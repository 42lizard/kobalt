#include "support/qt_compat.h"
#include "reader_alignment.h"
#include <lvtinydom.h>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <vector>

namespace kobalt {
namespace {
QString text(const lString32 &value) {
    return QString::fromUtf8(UnicodeToUtf8(value).c_str());
}
bool preserve(ldomNode *node, const QString &name) {
    static const QSet<QString> elements{"h1",      "h2",         "h3",      "h4",    "h5",     "h6",     "header",
                                        "figure",  "figcaption", "caption", "table", "img",    "image",  "svg",
                                        "picture", "object",     "video",   "audio", "canvas", "iframe", "pre",
                                        "code",    "head",       "style",   "script"};
    // Computed publisher styles cover arbitrary class names and inherited centering.
    const auto style = node->getStyle();
    if (!style.isNull() && style->text_align == css_ta_center) {
        return true;
    }
    if (elements.contains(name))
        return true;
    const auto role = text(node->getAttributeValue("role")).split(' ', kobalt::qt::skipEmptyParts);
    if (role.contains("heading"))
        return true;
    const auto type = text(node->getAttributeValue(LXML_NS_ANY, node->getDocument()->getAttrNameIndex("type")))
                          .split(' ', kobalt::qt::skipEmptyParts);
    for (const auto *value : {"title", "subtitle", "fulltitle", "covertitle", "halftitle", "titlepage"})
        if (type.contains(value))
            return true;
    // EPUBs also commonly encode headings as paragraphs with named classes.
    static const QRegularExpression headingClass("(?:^|[\\s_-])(?:title|subtitle|heading)(?:$|[\\s_-])",
                                                 QRegularExpression::CaseInsensitiveOption);
    return headingClass.match(text(node->getAttributeValue("class"))).hasMatch();
}
struct Frame {
    ldomNode *node;
    int child = 0;
    bool blocked = false, words = false, protectedChild = false, blockChild = false, block = false;
};
}
void markBodyAlignment(ldomDocument *document) {
    if (!document || !document->getRootNode())
        return;
    const auto attribute = document->getAttrNameIndex("data-kobalt-prose");
    static const QSet<QString> blocks{"p", "li", "dt", "dd", "blockquote", "div", "section", "article"};
    std::vector<Frame> stack{{document->getRootNode()}};
    while (!stack.empty()) {
        auto &frame = stack.back();
        auto *node = frame.node;
        if (!frame.child && node->isElement()) {
            const auto name = text(node->getNodeName());
            frame.block = blocks.contains(name);
            frame.blocked = frame.blocked || preserve(node, name);
            if (node->hasAttribute(attribute))
                node->setAttributeValue(0, attribute, U"");
        }
        if (frame.child < node->getChildCount()) {
            auto *child = node->getChildNode(frame.child++);
            stack.push_back({child, 0, frame.blocked});
            continue;
        }
        if (node->isText())
            frame.words = !text(node->getText()).trimmed().isEmpty();
        const bool block = frame.block;
        if (block && frame.words && !frame.blocked && !frame.protectedChild && !frame.blockChild)
            node->setAttributeValue(0, attribute, U"1");
        const auto finished = frame;
        stack.pop_back();
        if (!stack.empty()) {
            auto &parent = stack.back();
            parent.words |= finished.words;
            parent.protectedChild |= finished.blocked || finished.protectedChild;
            parent.blockChild |= block || finished.blockChild;
        }
    }
}
}
