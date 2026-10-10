#include "support/qt_compat.h"
#include "support/strings.h"
#include "reader_model.h"
#include "engine_activity.h"
#include <lvdocview.h>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace kobalt {
void ReaderModel::markKoboLinkDecorations() {
    auto *document = engine_->getDocument();
    const auto attribute = document->getAttrNameIndex("data-kobalt-kobo-link");
    QVector<ldomNode *> pending{document->getRootNode()};
    while (!pending.isEmpty()) {
        auto *node = pending.takeLast();
        if (!node || !node->isElement())
            continue;
        if (node->hasAttribute(attribute))
            node->setAttributeValue(0, attribute, U"");
        if (node->isNodeName("a") && !node->getAttributeValue("href").empty()) {
            bool wrapped = false, otherContent = false;
            for (int i = 0; i < node->getChildCount(); ++i) {
                auto *child = node->getChildNode(i);
                if (child->isText()) {
                    otherContent |= !QString::fromUtf8(UnicodeToUtf8(child->getText()).c_str()).trimmed().isEmpty();
                } else {
                    const auto classes = QString::fromUtf8(UnicodeToUtf8(child->getAttributeValue("class")).c_str())
                                             .split(QRegularExpression("\\s+"), qt::skipEmptyParts);
                    const bool wrapper = child->isNodeName("se-span") && classes.contains("kobo-link");
                    wrapped |= wrapper;
                    otherContent |= !wrapper;
                }
            }
            // Decorations propagate from the anchor through inline children.
            // Mark the anchor only when wrappers cover all its visible content.
            // This changes the in-memory DOM, never the publication bytes.
            if (wrapped && !otherContent)
                node->setAttributeValue(0, attribute, U"1");
        }
        for (int i = node->getChildCount() - 1; i >= 0; --i)
            pending.append(node->getChildNode(i));
    }
}
namespace {
QString text(const lString32 &value) {
    return QString::fromUtf8(UnicodeToUtf8(value).c_str());
}
lString32 domText(const QString &value) {
    return Utf8ToUnicode(value.toUtf8().constData());
}
bool token(ldomNode *node, const char *name, const char *value, bool anyNamespace = false) {
    const auto attribute =
        anyNamespace
            ? node->getAttributeValue(LXML_NS_ANY,
                                      node->getDocument()->getAttrNameIndex(domText(QString::fromLatin1(name)).c_str()))
            : node->getAttributeValue(name);
    return text(attribute)
        .split(QRegularExpression("\\s+"), kobalt::qt::skipEmptyParts)
        .contains(value, Qt::CaseInsensitive);
}
bool noteElement(ldomNode *node) {
    return token(node, "type", "footnote", true) || token(node, "type", "endnote", true) ||
           token(node, "role", "doc-footnote") || token(node, "role", "doc-endnote") ||
           token(node, "class", "footnote") || token(node, "class", "endnote");
}
// A block that starts another note or a new section.
bool endsNote(ldomNode *node) {
    const auto tag = text(node->getNodeName()).toLower();
    if (tag == "section" || tag == "aside" || tag == "hr" ||
        (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') ||
        !node->getAttributeValue("id").empty() || noteElement(node))
        return true;
    for (int i = 0; i < node->getChildCount(); ++i) {
        auto *child = node->getChildNode(i);
        if (child->isElement() && endsNote(child))
            return true;
    }
    return false;
}
// Copy note text and basic structure only. Publisher styles, scripts and
// resources must not reach the popup's QTextDocument or start external I/O.
bool noteHtml(ldomNode *node, QString &html, int &nodes, int depth = 0) {
    constexpr int textLimit = 128 * 1024;
    engineActivity().checkpoint();
    if (!node || ++nodes > 8192 || depth > 64 || html.size() > textLimit)
        return false;
    if (node->isText()) {
        const auto value = node->getText();
        if (value.length() > textLimit)
            return false;
        html += text(value).toHtmlEscaped();
        return html.size() <= textLimit;
    }
    const auto tag = text(node->getNodeName()).toLower();
    if (tag == "script" || tag == "style" || tag == "head")
        return true;
    if (tag == "img" || tag == "svg") {
        const auto alt = text(node->getAttributeValue("alt")).left(1024);
        html += "[" + (alt.isEmpty() ? strings::get("common.image").toHtmlEscaped() : alt.toHtmlEscaped()) + "]";
        return html.size() <= textLimit;
    }
    static const QSet<QString> allowed{"p",          "div",   "span", "b",     "strong", "i",  "em", "u",
                                       "s",          "sub",   "sup",  "br",    "hr",     "ul", "ol", "li",
                                       "blockquote", "pre",   "code", "table", "tr",     "td", "th", "thead",
                                       "tbody",      "tfoot", "dl",   "dt",    "dd"};
    const auto output =
        allowed.contains(tag) ? tag
        : tag == "aside" || tag == "section" || (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6')
            ? QStringLiteral("div")
            : QString();
    if (!output.isEmpty())
        html += "<" + output + ">";
    for (int i = 0; i < node->getChildCount(); ++i)
        if (!noteHtml(node->getChildNode(i), html, nodes, depth + 1))
            return false;
    if (!output.isEmpty() && output != "br" && output != "hr")
        html += "</" + output + ">";
    return html.size() <= textLimit;
}
}
QVector<PageLink> ReaderModel::pageLinks() {
    QVector<PageLink> result;
    if (!engine_)
        return result;
    ldomXRangeList links;
    engine_->getCurrentPageLinks(links);
    for (int i = 0; i < links.length(); ++i) {
        engineActivity().checkpoint();
        auto *range = links[i];
        ldomXPointer source;
        if (range->getHRef(source).empty() || source.isNull())
            continue;
        const auto rectangles = rangeRects(text(range->getStart().toString()), text(range->getEnd().toString()));
        if (!rectangles.isEmpty())
            result.append({text(source.toString()), rectangles});
    }
    return result;
}
LinkDetails ReaderModel::linkDetails(const QString &source) {
    LinkDetails result;
    if (!engine_)
        return result;
    auto *doc = engine_->getDocument();
    auto pointer = doc->createXPointer(domText(source));
    ldomXPointer link;
    const auto href = pointer.getHRef(link);
    if (href.empty() || link.isNull())
        return result;
    auto *node = link.getNode();
    result.label = text(node->getText(' ', 256)).trimmed();
    result.kind = LinkKind::Unavailable;
    result.error = strings::get("error.this_passage_is_unavailable");
    if (href[0] != '#') {
        const QUrl url(text(href));
        if (url.isValid() && !url.host().isEmpty() && (url.scheme() == "https" || url.scheme() == "http")) {
            result.kind = LinkKind::Website;
            result.target = url.toString();
            result.error.clear();
        }
        return result;
    }
    auto *target = doc->getNodeById(doc->getAttrValueIndex(href.substr(1).c_str()));
    if (!target)
        return result;
    auto *note = target;
    bool isNote = token(node, "type", "noteref", true) || token(node, "role", "doc-noteref") ||
                  token(node, "rel", "footnote") || token(node, "type", "note");
    bool contained = false;
    for (auto *parent = target; parent && text(parent->getNodeName()) != "DocFragment";
         parent = parent->getParentNode()) {
        if (noteElement(parent)) {
            note = parent;
            isNote = contained = true;
            break;
        }
    }
    ldomXPointer destination(target, 0);
    if (destination.toPoint().y >= 0) {
        result.target = text(destination.toString());
        result.chapter = chapterTitle(result.target);
    }
    if (!isNote) {
        if (!result.target.isEmpty()) {
            result.kind = LinkKind::Passage;
            result.error.clear();
        }
        return result;
    }
    // Older EPUB notes put the ID on an empty anchor inside a paragraph.
    // Include that paragraph without pulling in the whole chapter of notes.
    while (note && (text(note->getNodeName()) == "a" || text(note->getNodeName()) == "span"))
        note = note->getParentNode();
    result.kind = LinkKind::Footnote;
    result.error.clear();
    if (!note || text(note->getNodeName()) == "body" || text(note->getNodeName()) == "DocFragment" ||
        text(note->getNodeName()) == "html") {
        result.error = strings::get("error.this_note_has_no_separate_preview");
        return result;
    }
    // Without a note container, a note can continue in the blocks after the
    // one that holds its ID. Take them up to the next ID or heading.
    QVector<ldomNode *> parts{note};
    auto *parent = note->getParentNode();
    for (int i = note->getNodeIndex() + 1; !contained && parent && i < parent->getChildCount(); ++i) {
        auto *next = parent->getChildNode(i);
        if (!next->isElement())
            continue;
        if (endsNote(next))
            break;
        parts.append(next);
    }
    int visited = 0;
    bool complete = true;
    for (auto *part : parts)
        complete = complete && noteHtml(part, result.html, visited);
    if (!complete) {
        result.html.clear();
        result.error = strings::get("error.this_note_is_too_long_to_preview");
    } else if (result.html.trimmed().isEmpty())
        result.error = strings::get("error.this_note_has_no_text_to_preview");
    return result;
}
}
