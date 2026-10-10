#pragma once
#include "storage/bookmark_store.h"
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QVector>
#include <functional>
#include <memory>

namespace kobalt {
class ReaderSession;

QString bookmarkLayoutKey(int pages, const QJsonObject &state, const QSize &pageArea);

// Kobalt's own bookmarks for one open book, one per page. The engine thread
// looks up the page of each stored bookmark whenever the layout changes.
// Bookmarks that end up on one page are merged, and every change is saved.
//
// The window names the current layout, because the page area size is part of
// it. The owner destroys this before the ReaderSession it borrows. Replies
// from the engine are dropped after destruction or a layout change. A page
// lookup is also repeated when the list changed meanwhile, because its pages
// pair with the old list.
class BookmarkSession final : public QObject {
public:
    BookmarkSession(ReaderSession &model, std::function<QString()> layout, const QString &annotations,
                    const QString &identity, const QByteArray &fingerprint, const QString &title);
    // False when the book has no stable key. The session then does nothing.
    bool available() const {
        return bool(store);
    }
    bool writable() const {
        return store && !loadFailed_;
    }
    QString error() const {
        return error_;
    }
    const QVector<Bookmark> &bookmarks() const;
    // Starts a page lookup when the layout is new. True when the pages of
    // the current layout are known.
    bool sync();
    bool resolved() const;
    bool marked(int page) const;
    // The page of the bookmark at `index` in the current layout, or -1.
    int page(int index) const;
    // The page of the bookmark at `position` in the current layout, or -1.
    int pageOf(const QString &position) const;
    enum class Toggle { Refused, Removed, Adding };
    // Removes the bookmark on `page`, or starts adding one there.
    Toggle toggle(int page);
    bool remove(const QString &position);
    const QVector<int> &pages() const {
        return pages_;
    }
    // The known pages changed: a lookup finished, needs repeating, or merged
    // bookmarks. The window refreshes.
    std::function<void()> changed;
    // A bookmark from toggle() was added.
    std::function<void()> added;
    // Loading or saving failed. The list and its saved bytes are preserved.
    std::function<void()> failed;

private:
    void merge();
    bool save(QVector<Bookmark> bookmarks, QVector<int> pages, const char *reason);
    ReaderSession &model;
    std::function<QString()> layout;
    std::unique_ptr<BookmarkStore> store;
    QString error_;
    bool loadFailed_ = false;
    // pages_[i] is the page of store->bookmarks()[i] in layout_. While
    // layout_ is set, pages_ has one entry per bookmark.
    QVector<int> pages_;
    QString layout_, lookup_;
    // Changes with every edit of the list, so an older page lookup is repeated.
    quint64 generation = 0;
    // Pages with an add waiting for the engine. A second tap there waits too.
    QSet<int> adding;
};
}
