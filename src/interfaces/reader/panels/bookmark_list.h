#pragma once
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/paging.h"
#include "interfaces/reader/ui/controls/panels.h"
#include <QDateTime>
#include <functional>

namespace kobalt {
struct BookmarkRow {
    QString position, chapter, excerpt;
    QDateTime created;
    int page = -1; // Zero-based, or -1 when the position no longer resolves.
    bool current = false;
    int endPage = -1;
};

// The Bookmarks list covers the header's action row with its own bar, like
// the Chapters list. Rows go to their page; each row's remove cell asks first.
class BookmarkList : public ui::Panel {
public:
    explicit BookmarkList(QWidget *parent);
    void showBookmarks(QVector<BookmarkRow> rows);
    void showAnnotations(QVector<BookmarkRow> rows);
    void setExcerptFont(const QString &family);
    bool annotationsShown() const {
        return annotations;
    }
    std::function<void(bool)> tabRequested;
    std::function<void(const QString &)> selected, removeRequested;
    std::function<void()> back;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void arrange();
    void redraw();
    int pageCount() const;
    QVector<BookmarkRow> items;
    QVector<QPushButton *> buttons, removes;
    QPushButton *bookmarksTab, *annotationsTab;
    bool annotations = false;
    QString excerptFamily;
    int listTop() const;
    int rowHeight() const;
    QFont excerptFont() const;
    QLabel *summary, *pageHeading, *empty, *emptyHint;
    ui::ListHeader *listHeader;
    ui::CellButton *close;
    ui::PagerBar *pager;
    int page = 0, rows = 1;
};
}
