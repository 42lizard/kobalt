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
};

// The Bookmarks list covers the header's action row with its own bar, like
// the Chapters list. Rows go to their page; each row's remove cell asks first.
class BookmarkList : public ui::Panel {
public:
    explicit BookmarkList(QWidget *parent);
    void showBookmarks(QVector<BookmarkRow> rows, const QString &position);
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
    QLabel *title, *position, *summary, *pageHeading, *empty, *emptyHint;
    ui::ListHeader *listHeader;
    ui::CellButton *close;
    ui::PagerBar *pager;
    int page = 0, rows = 1;
};
}
