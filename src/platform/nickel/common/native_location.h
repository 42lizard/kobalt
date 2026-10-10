#pragma once
#include <QString>

namespace kobalt {
// Older Kobo bookmarks prefix the spine path with the book ID and use ! for
// path separators. Only decode that form when it belongs to this publication.
inline QString publicationLocation(const QString &book, const QString &location) {
    if (book.isEmpty() || !location.startsWith(book + '!'))
        return location;
    const int fragment = location.indexOf('#', book.size() + 1);
    auto path = location.mid(book.size() + 1, fragment < 0 ? -1 : fragment - book.size() - 1);
    path.replace('!', '/');
    return path + (fragment < 0 ? QString() : location.mid(fragment));
}
}
