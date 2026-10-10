#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

namespace kobalt {
// A bookmark marks the page that starts at `position`, a CREngine XPointer.
struct Bookmark {
    QString position;
    QString chapter;   // The chapter title when the bookmark was made.
    QString excerpt;   // The first words of the page.
    QDateTime created; // UTC.
    bool operator==(const Bookmark &other) const {
        return position == other.position && chapter == other.chapter && excerpt == other.excerpt &&
               created == other.created;
    }
};

// Kobalt's own bookmarks, separate from Nickel's. Each book has a folder in
// `root` (.adds/kobalt/annotations). A purchased book's folder is named after
// Kobo's book ID, which stays the same across downloads; a sideloaded book's
// after its content fingerprint, which stays the same when the file moves.
class BookmarkStore {
public:
    static constexpr int maxBookmarks = 2000;
    static constexpr qint64 maxFileBytes = 1024 * 1024;

    // The folder name for a book, or an empty string when it has no stable key.
    static QString bookKey(const QString &identity, const QByteArray &fingerprint);

    BookmarkStore(QString root, QString identity, QByteArray fingerprint, QString title);
    // Reads the book's folder. A sideloaded file that changed has a new
    // fingerprint; its bookmarks then come from the folder that last saw its
    // path, and the next save writes them to the new folder.
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    bool available() const {
        return !key_.isEmpty();
    }
    QString folder() const;
    QString adoptedFrom() const {
        return adoptedFrom_;
    }

    const QVector<Bookmark> &bookmarks() const {
        return bookmarks_;
    }
    void setBookmarks(QVector<Bookmark> bookmarks);

    // One bookmark per page: bookmarks whose `page` is the same become one,
    // made by `atPage` from that page and the oldest creation date. Bookmarks
    // whose position no longer resolves (page < 0) are kept unchanged.
    // Returns whether anything changed.
    bool merge(const std::function<int(const QString &)> &page,
               const std::function<Bookmark(int page, const QDateTime &created)> &atPage);

private:
    QString root_, identity_, title_, key_, adoptedFrom_;
    QByteArray fingerprint_;
    QVector<Bookmark> bookmarks_;
    bool loadFailed_ = false;
    bool readFolder(const QString &folder, QString *error);
};

// The excerpt without the chapter title it starts with. A page that opens a
// chapter starts with its heading, which the row already shows as the chapter.
// Returns the excerpt unchanged when nothing else would be left.
QString excerptAfterChapter(const QString &excerpt, const QString &chapter);

QJsonObject bookmarkJson(const Bookmark &bookmark);
bool bookmarkFromJson(const QJsonObject &object, Bookmark *bookmark);
}
