#include "support/qt_compat.h"
#include "support/strings.h"
#include "layout_compatibility.h"
#include <QElapsedTimer>
#include <QSet>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace kobalt {
const char *LayoutCompatibility::reasonKey() const {
    switch (verdict) {
    case Supported:
        return "";
    case VerticalWriting:
        return strings::key("error.publication_declares_vertical_writing");
    case RightToLeftProgression:
        return strings::key("error.publication_requires_right_to_left_page_navigation");
    case Incomplete:
        return strings::key("error.publication_layout_inspection_is_incomplete");
    }
    return strings::key("error.publication_layout_inspection_is_incomplete");
}
QString LayoutCompatibility::reason() const {
    return verdict == Supported ? QString() : strings::get(reasonKey());
}
namespace {
constexpr qint64 resourceLimit = 2 * 1024 * 1024, totalLimit = 8 * 1024 * 1024;
constexpr int resourceCount = 512, timeLimit = 15000;
struct Token {
    QString value;
    bool identifier = false, string = false;
};
// Preserve strings and comments as tokens so examples in generated content do
// not look like declarations. CSS escapes also apply to property names/URLs.
bool tokenize(const QString &css, QVector<Token> &tokens) {
    int i = 0;
    auto escape = [&](QString &out) {
        if (++i >= css.size())
            return false;
        uint code = 0;
        int digits = 0;
        while (i < css.size() && digits < 6) {
            const auto c = css[i].toLower().unicode();
            const int hex = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (hex < 0)
                break;
            code = code * 16 + uint(hex);
            ++i;
            ++digits;
        }
        if (digits) {
            if (i < css.size() && css[i].isSpace())
                ++i;
            if (!code || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                code = 0xfffd;
            out += kobalt::qt::character(char32_t(code));
        } else {
            out += css[i++];
        }
        return true;
    };
    while (i < css.size()) {
        if (tokens.size() >= 200000)
            return false;
        if (css[i].isSpace()) {
            ++i;
            continue;
        }
        if (css.mid(i, 2) == "/*") {
            const int end = css.indexOf("*/", i + 2);
            if (end < 0)
                return false;
            i = end + 2;
            continue;
        }
        Token token;
        if (css[i] == '\'' || css[i] == '"') {
            const auto quote = css[i++];
            token.string = true;
            while (i < css.size() && css[i] != quote) {
                if (css[i] == '\\') {
                    if (!escape(token.value))
                        return false;
                } else
                    token.value += css[i++];
            }
            if (i == css.size())
                return false;
            ++i;
        } else if (css[i].isLetterOrNumber() || css[i] == '-' || css[i] == '_' || css[i] == '\\') {
            token.identifier = true;
            while (i < css.size() && (css[i].isLetterOrNumber() || css[i] == '-' || css[i] == '_' || css[i] == '\\')) {
                if (css[i] == '\\') {
                    if (!escape(token.value))
                        return false;
                } else
                    token.value += css[i++];
            }
        } else
            token.value = css[i++];
        tokens.append(token);
    }
    return true;
}
class Inspection {
public:
    ResourceCache &cache;
    const QMap<QString, qint64> &sizes;
    LayoutCompatibility result;
    QElapsedTimer clock;
    QSet<QString> seen;
    QVector<QPair<QString, bool>> pending;
    bool vertical = false, verticalHint = false, verticalStyle = false, complete = true;
    Inspection(ResourceCache &cache, const QMap<QString, qint64> &sizes) : cache(cache), sizes(sizes) {
        clock.start();
    }
    QString resolve(const QString &base, const QString &reference) {
        const QUrl relative(reference);
        if (reference.isEmpty() || !relative.isValid() || !relative.scheme().isEmpty() ||
            !relative.authority().isEmpty() || relative.hasQuery() || reference.contains('\\')) {
            complete = false;
            return {};
        }
        QUrl origin;
        origin.setScheme("https");
        origin.setHost("publication.invalid");
        origin.setPath('/' + base);
        const auto url = origin.resolved(relative).adjusted(QUrl::NormalizePathSegments);
        const auto name = url.path(QUrl::FullyDecoded).mid(1);
        if (!sizes.contains(name)) {
            complete = false;
            return {};
        }
        return name;
    }
    QByteArray read(const QString &name) {
        if (name.isEmpty() || clock.elapsed() > timeLimit || result.resources >= resourceCount ||
            sizes.value(name, -1) < 0 || sizes.value(name) > resourceLimit ||
            sizes.value(name) > totalLimit - result.bytes || result.bytes >= totalLimit) {
            complete = false;
            return {};
        }
        ++result.resources;
        const auto data = cache.readBounded(name, qMin(resourceLimit, totalLimit - result.bytes));
        if (data.isNull())
            complete = false;
        result.bytes += data.size();
        return data;
    }
    void queue(const QString &base, const QString &href, bool css) {
        const auto name = resolve(base, href);
        if (name.isEmpty() || seen.contains(name))
            return;
        if (seen.size() >= resourceCount) {
            complete = false;
            return;
        }
        seen.insert(name);
        pending.append({name, css});
    }
    void mode(QString value, bool metadata = false, bool supportedProperty = true) {
        value = value.trimmed().toLower();
        if (value == "vertical-rl" && (metadata || supportedProperty)) {
            if (metadata)
                verticalHint = true;
            else
                verticalStyle = true;
            return;
        }
        if (QStringList{"vertical-rl", "vertical-lr", "sideways-rl", "sideways-lr", "tb", "tb-rl", "tb-lr"}.contains(
                value))
            vertical = true;
        else if (metadata && value == "horizontal-rl")
            complete = false;
        else if (!QStringList{"horizontal-tb", "horizontal-lr", "lr", "lr-tb", "rl", "rl-tb", "initial", "inherit",
                              "unset", "revert", "revert-layer"}
                      .contains(value))
            complete = false;
    }
    void css(const QString &base, const QString &source) {
        QVector<Token> tokens;
        if (!tokenize(source, tokens)) {
            complete = false;
            return;
        }
        for (int i = 0; i < tokens.size(); ++i) {
            const auto &token = tokens[i];
            const QString name = token.value.toLower();
            if (token.identifier &&
                (name == "writing-mode" || name == "-epub-writing-mode" || name == "-webkit-writing-mode" ||
                 name == "-ms-writing-mode" || name == "-moz-writing-mode" || name == "-o-writing-mode") &&
                i + 2 < tokens.size() && tokens[i + 1].value == ":") {
                const auto &value = tokens[i + 2];
                if (!value.identifier || (i + 3 < tokens.size() && tokens[i + 3].value == "("))
                    complete = false;
                else
                    mode(value.value, false,
                         name == "writing-mode" || name == "-epub-writing-mode" || name == "-webkit-writing-mode");
            }
            // text-orientation (including prefixed forms) only changes glyphs
            // in vertical writing. It does not make horizontal text vertical.
            if (token.value != "@" || i + 1 >= tokens.size() || !tokens[i + 1].identifier)
                continue;
            const auto rule = tokens[i + 1].value.toLower();
            if (rule == "charset") {
                if (i + 2 >= tokens.size() || tokens[i + 2].value.compare("utf-8", Qt::CaseInsensitive))
                    complete = false;
            } else if (rule == "import") {
                int at = i + 2;
                if (at < tokens.size() && tokens[at].string)
                    queue(base, tokens[at].value, true);
                else if (at + 2 < tokens.size() && tokens[at].value.compare("url", Qt::CaseInsensitive) == 0 &&
                         tokens[at + 1].value == "(") {
                    at += 2;
                    QString url;
                    while (at < tokens.size() && tokens[at].value != ")")
                        url += tokens[at++].value;
                    if (at == tokens.size())
                        complete = false;
                    else
                        queue(base, url, true);
                } else
                    complete = false;
            }
        }
    }
    void document(const QString &path) {
        QXmlStreamReader xml(read(path));
        while (!xml.atEnd() && complete && !vertical) {
            xml.readNext();
            if (xml.isProcessingInstruction() && xml.processingInstructionTarget().toString() == "xml-stylesheet")
                complete = false;
            if (!xml.isStartElement())
                continue;
            const auto name = xml.name().toString().toLower();
            const auto attrs = xml.attributes();
            // CREngine does not execute scripts. Their presence cannot establish
            // a vertical layout; inspect the publication's actual CSS instead.
            if (name == "script") {
                xml.skipCurrentElement();
                continue;
            }
            if (attrs.hasAttribute("http://www.w3.org/XML/1998/namespace", "base") || name == "base")
                complete = false;
            if (attrs.hasAttribute("style"))
                css(path, attrs.value("style").toString());
            for (const auto &a : attrs) {
                if (a.name().toString().compare("writing-mode", Qt::CaseInsensitive) == 0)
                    mode(a.value().toString(), false, false);
            }
            if (name == "style")
                css(path, xml.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            if (name == "link" && attrs.value("rel")
                                      .toString()
                                      .toLower()
                                      .split(QRegularExpression("\\s+"), kobalt::qt::skipEmptyParts)
                                      .contains("stylesheet"))
                queue(path, attrs.value("href").toString(), true);
        }
        if (xml.hasError())
            complete = false;
    }
    LayoutCompatibility run() {
        QXmlStreamReader container(read("META-INF/container.xml"));
        QString package;
        while (!container.atEnd()) {
            container.readNext();
            if (container.isStartElement() && container.name().toString() == "rootfile") {
                if (!package.isEmpty()) {
                    complete = false;
                    break;
                }
                package = resolve({}, container.attributes().value("full-path").toString());
            }
        }
        if (container.hasError() || package.isEmpty())
            complete = false;
        QMap<QString, QString> manifest;
        QMap<QString, QMap<QString, QString>> manifestAttributes;
        QStringList spine;
        QXmlStreamReader opf(complete ? read(package) : QByteArray());
        while (!opf.atEnd() && complete) {
            opf.readNext();
            if (!opf.isStartElement())
                continue;
            const auto name = opf.name().toString();
            const auto a = opf.attributes();
            if (a.hasAttribute("http://www.w3.org/XML/1998/namespace", "base"))
                complete = false;
            if (name == "meta") {
                const auto key =
                    a.value("name").isEmpty() ? a.value("property").toString() : a.value("name").toString();
                if (key.contains("writing-mode", Qt::CaseInsensitive))
                    mode(a.hasAttribute("content") ? a.value("content").toString() : opf.readElementText(), true);
            } else if (name == "item") {
                const auto id = a.value("id").toString();
                const auto media = a.value("media-type").toString();
                QMap<QString, QString> attributes;
                for (const auto &attribute : a) {
                    attributes[attribute.namespaceUri().toString() + QChar(0) + attribute.name().toString()] =
                        attribute.value().toString();
                }
                if (id.isEmpty()) {
                    complete = false;
                    break;
                }
                if (manifest.contains(id)) {
                    // Repeated identical items do not change resource lookup or
                    // layout. Conflicting declarations remain ambiguous.
                    if (manifestAttributes.value(id) != attributes) {
                        complete = false;
                        break;
                    }
                    continue;
                }
                manifestAttributes.insert(id, attributes);
                manifest[id] = a.value("href").toString();
                // Include non-linear and linked reading sections, not just the
                // first chapter. Fonts, images and audio are never requested.
                if (media == "application/xhtml+xml" || media == "text/html")
                    queue(package, manifest[id], false);
            } else if (name == "spine") {
                const auto direction = a.value("page-progression-direction").toString();
                if (!direction.isEmpty() && direction != "rtl" && direction != "ltr" && direction != "default")
                    complete = false;
            } else if (name == "itemref")
                spine.append(a.value("idref").toString());
        }
        if (opf.hasError() || spine.isEmpty() || pending.isEmpty())
            complete = false;
        for (const auto &id : spine) {
            if (!manifest.contains(id))
                complete = false;
            else {
                const auto path = resolve(package, manifest[id]);
                if (!seen.contains(path))
                    complete = false;
            }
        }
        for (int i = 0; i < pending.size() && complete && !vertical; ++i) {
            const auto entry = pending[i];
            if (entry.second) {
                const auto bytes = read(entry.first);
                const auto utf8 = bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes;
                const auto text = QString::fromUtf8(utf8);
                if (text.toUtf8() != utf8)
                    complete = false;
                else
                    css(entry.first, text);
            } else
                document(entry.first);
        }
        result.transientFailure = clock.elapsed() > timeLimit ||
                                  (!cache.error().isEmpty() && cache.failure() != ResourceCache::Failure::Limit);
        if (result.transientFailure)
            complete = false;
        result.verdict = (vertical || (verticalHint && !verticalStyle)) ? LayoutCompatibility::VerticalWriting
                         : complete                                     ? LayoutCompatibility::Supported
                                                                        : LayoutCompatibility::Incomplete;
        return result;
    }
};
}
LayoutCompatibility inspectLayout(ResourceCache &resources, const QMap<QString, qint64> &sizes) {
    return Inspection(resources, sizes).run();
}
}
