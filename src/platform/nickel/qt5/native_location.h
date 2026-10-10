#pragma once
#include <QString>
#include <QUrl>

// Firmware 4 stores a reading position as a chapter path and an optional
// element ID, for example "OEBPS/chapter1.xhtml#kobo.9.3". It has no text
// offset. The shared reader uses the same parts with percent-encoding and an
// optional ":offset".
namespace kobalt::nickel4 {
// Older Kobo bookmarks prefix the chapter path with the book and use ! for
// path separators. Firmware 4 chapter rows use a plain file path as that
// prefix. Only decode the form when it belongs to this publication.
inline QString chapter(const QString &book, const QString &raw) {
    for (const auto &prefix : {book + '!', QUrl(book).toLocalFile() + '!'}) {
        if (prefix.size() < 2 || !raw.startsWith(prefix))
            continue;
        const int hash = raw.indexOf('#', prefix.size());
        auto path = raw.mid(prefix.size(), hash < 0 ? -1 : hash - prefix.size());
        path.replace('!', '/');
        return path + (hash < 0 ? QString() : raw.mid(hash));
    }
    return raw;
}
inline QString shared(const QString &raw) {
    const int hash = raw.indexOf('#');
    const auto path = QString::fromLatin1(QUrl::toPercentEncoding(raw.left(hash < 0 ? raw.size() : hash), "/"));
    return hash < 0 ? path : path + '#' + QString::fromLatin1(QUrl::toPercentEncoding(raw.mid(hash + 1)));
}
// Nickel receives the element that contains the position. The exact offset
// stays in Kobalt's local checkpoint. Returns an empty string if unsupported.
inline QString stored(const QString &position) {
    const int hash = position.indexOf('#');
    const auto path = QUrl::fromPercentEncoding(position.left(hash < 0 ? position.size() : hash).toUtf8());
    if (path.isEmpty() || path.contains('#'))
        return {};
    if (hash < 0)
        return path;
    auto id = position.mid(hash + 1);
    const int colon = id.lastIndexOf(':');
    if (colon >= 0) {
        bool ok = false;
        if (id.mid(colon + 1).toInt(&ok) < 0 || !ok)
            return {};
        id.truncate(colon);
    }
    id = QUrl::fromPercentEncoding(id.toUtf8());
    if (id.isEmpty() || id.contains('#'))
        return {};
    return path + '#' + id;
}
}
