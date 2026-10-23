#include "interfaces/reader/ui/controls/text.h"
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/paging.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/style/theme.h"
#include "support/qt_font.h"
#include "support/strings.h"
#include "bookmark_list.h"
#include "interfaces/reader/ui/style/styles.h"
#include "interfaces/reader/ui/style/fonts.h"
#include <QLocale>
#include <QPainter>

namespace kobalt {
namespace c = ui::metrics::chrome;
namespace strip = ui::metrics::contents;
BookmarkList::BookmarkList(QWidget *parent) : Panel(parent) {
    setObjectName("rrBookmarkList");
    close = new ui::CellButton(ui::ControlIcon::Close, this);
    close->setObjectName("rrBookmarkListClose");
    close->setRule(Qt::RightEdge);
    close->setAccessibleName(strings::get("bookmarks.close"));
    connect(close, &QPushButton::clicked, this, [this] {
        if (back)
            back();
    });
    listHeader = new ui::ListHeader(this);
    listHeader->setTrailingInset(c::cellWidth);
    summary = listHeader->leftLabel();
    summary->setObjectName("rrBookmarkListSummary");
    pageHeading = listHeader->rightLabel();
    pageHeading->setObjectName("rrBookmarkListPageHeading");
    for (auto *label : {summary, pageHeading}) {
        label->setFont(ui::interfaceFont(c::captionText));
        ui::readerStyle(label, ui::styles::font(c::captionText) +
                                   "background:transparent;border:none;color:palette(placeholder-text);");
    }
    empty = new ui::InterfaceLabel(strings::get("bookmarks.empty"), this);
    empty->setObjectName("rrBookmarkListEmpty");
    empty->setTextFormat(Qt::PlainText);
    empty->setAlignment(Qt::AlignCenter);
    ui::readerStyle(empty, ui::styles::font(c::labelText) + "background:transparent;");
    emptyHint = new ui::InterfaceLabel(strings::get("bookmarks.empty_hint"), this);
    emptyHint->setObjectName("rrBookmarkListEmptyHint");
    emptyHint->setTextFormat(Qt::PlainText);
    emptyHint->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyHint->setWordWrap(true);
    ui::readerStyle(emptyHint,
                    ui::styles::font(c::descriptionText) + "color:palette(placeholder-text);background:transparent;");
    pager = new ui::PagerBar(this, "rrBookmarkList");
    pager->previous->setAccessibleName(strings::get("bookmarks.previous"));
    pager->next->setAccessibleName(strings::get("bookmarks.next"));
    connect(pager->previous, &QPushButton::clicked, this, [this] {
        if (pageCount() > 1) {
            page = pager->turnTarget(-1);
            redraw();
        }
    });
    connect(pager->next, &QPushButton::clicked, this, [this] {
        if (pageCount() > 1) {
            page = pager->turnTarget(1);
            redraw();
        }
    });
    bookmarksTab = new ui::Button(strings::get("bookmarks.title"), this);
    annotationsTab = new ui::Button(strings::get("annotations.tab"), this);
    bookmarksTab->setObjectName("rrBookmarksTab");
    annotationsTab->setObjectName("rrAnnotationsTab");
    for (auto *tab : {bookmarksTab, annotationsTab}) {
        tab->setCheckable(true);
        static_cast<ui::Button *>(tab)->setPaintMode(ui::ButtonPaintMode::ToolbarCell);
        tab->setFont(ui::brandFont(c::titleText));
        ui::readerStyle(tab, "QPushButton{border:none;padding:0;min-height:0;" + ui::styles::brand(c::titleText) + "}");
    }
    connect(bookmarksTab, &QPushButton::clicked, this, [this] {
        if (tabRequested)
            tabRequested(false);
    });
    connect(annotationsTab, &QPushButton::clicked, this, [this] {
        if (tabRequested)
            tabRequested(true);
    });
    hide();
}
void BookmarkList::setExcerptFont(const QString &family) {
    if (excerptFamily == family) {
        return;
    }
    excerptFamily = family;
    arrange();
    redraw();
}
QFont BookmarkList::excerptFont() const {
    QFont font = excerptFamily.isEmpty() ? ui::interfaceFont() : QFont(excerptFamily);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    font.setPixelSize(ui::metrics::search::excerptText);
    return font;
}
int BookmarkList::rowHeight() const {
    const auto font = excerptFont();
    const auto sample = QStringLiteral("Mg");
    const bool privateText = ui::needsPrivateText(sample, font);
    const QFontMetrics metrics(font);
    const int excerptHeight = privateText ? qMax(ui::measurePrivateTextSize(sample, font).height(),
                                                 ui::renderPrivateText(sample, font).image.height())
                                          : qMax(metrics.height(), metrics.tightBoundingRect(sample).height());
    const int lineHeight = qMax(excerptHeight, QFontMetrics(ui::interfaceFont(c::labelText)).height());
    return qMax(int(ui::metrics::list::threeLines),
                2 * int(ui::metrics::list::lineInset) + 3 * lineHeight + int(ui::metrics::control::border));
}
int BookmarkList::pageCount() const {
    return qMax(1, (int(items.size()) + rows - 1) / rows);
}
int BookmarkList::listTop() const {
    return c::actionHeight + strip::stripHeight;
}
void BookmarkList::showAnnotations(QVector<BookmarkRow> list) {
    const bool sameTab = annotations && isVisible();
    const int previousPage = page;
    showBookmarks(std::move(list));
    annotations = true;
    page = sameTab ? previousPage : 0;
    redraw();
}
void BookmarkList::showBookmarks(QVector<BookmarkRow> list) {
    annotations = false;
    // Keep the list page when the same list comes back after a removal.
    const bool reopening = !isVisible();
    items = std::move(list);
    arrange();
    if (reopening) {
        page = 0;
        for (int i = 0; i < items.size(); ++i)
            if (items[i].current)
                page = i / rows;
    }
    redraw();
    show();
    raise();
}
void BookmarkList::resizeEvent(QResizeEvent *) {
    arrange();
    redraw();
}
void BookmarkList::paintEvent(QPaintEvent *event) {
    ui::Panel::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(0, c::actionHeight - c::heavyRule, width(), c::heavyRule, palette().color(QPalette::WindowText));
    painter.fillRect(0, listTop() - ui::metrics::control::border, width(), ui::metrics::control::border,
                     ui::dividerColor(this));
}
void BookmarkList::arrange() {
    const int first = page * rows, top = listTop(), available = height() - top, itemHeight = rowHeight();
    // Leave room for the pager only when the list needs more than one page.
    rows = qMax(1, available / itemHeight);
    if (items.size() > rows)
        rows = qMax(1, (available - c::actionHeight) / itemHeight);
    page = first / rows;
    const int barHeight = c::actionHeight - c::heavyRule;
    const int tabWidth = qMax(1, (width() - c::cellWidth) / 2);
    close->setGeometry(0, 0, c::cellWidth, barHeight);
    annotationsTab->setGeometry(c::cellWidth, 0, tabWidth, barHeight);
    bookmarksTab->setGeometry(c::cellWidth + tabWidth, 0, qMax(1, width() - c::cellWidth - tabWidth), barHeight);
    listHeader->setGeometry(0, c::actionHeight, width(), ui::metrics::list::headerHeight);
    pager->setGeometry(0, height() - c::actionHeight, width(), c::actionHeight);
    empty->setGeometry(c::inset, top + itemHeight / 2, qMax(1, width() - 2 * c::inset), itemHeight / 2);
    emptyHint->setGeometry(2 * c::inset, top + itemHeight, qMax(1, width() - 4 * c::inset), itemHeight);
}
void BookmarkList::redraw() {
    const int itemHeight = rowHeight();
    bookmarksTab->setChecked(!annotations);
    annotationsTab->setChecked(annotations);
    empty->setText(strings::get(annotations ? "annotations.empty" : "bookmarks.empty"));
    emptyHint->setText(strings::get(annotations ? "annotations.empty_hint" : "bookmarks.empty_hint"));
    page = qBound(0, page, pageCount() - 1);
    empty->setVisible(items.isEmpty());
    emptyHint->setVisible(items.isEmpty());
    pageHeading->setVisible(!items.isEmpty());
    listHeader->setCaptions(strings::get(annotations ? "annotations.summary" : "bookmarks.summary").arg(items.size()),
                            items.isEmpty() ? QString() : strings::get("chapter_contents.page"));
    pager->setVisible(items.size() > rows);
    pager->setPage(page, pageCount(), {}, true);
    const int used = listTop() + qMin(rows, qMax(0, int(items.size()) - page * rows)) * rowHeight();
    pager->setBlankArea({c::inset, used, width() - 2 * c::inset, pager->y() - used});
    while (buttons.size() < qMin(rows, int(items.size()))) {
        const int row = buttons.size();
        auto *button = new ui::ListRow(this);
        auto *remove = button->addAction({}, strings::get("bookmarks.remove"));
        // An unresolved bookmark still needs its independent removal action.
        remove->setParent(this);
        buttons.append(button);
        removes.append(remove);
        connect(button, &QPushButton::clicked, this, [this, row] {
            const int index = page * rows + row;
            if (index < items.size() && items[index].page >= 0 && selected)
                selected(items[index].position);
        });
        connect(remove, &QPushButton::clicked, this, [this, row] {
            const int index = page * rows + row;
            if (index < items.size()) {
                if (removeRequested) {
                    removeRequested(items[index].position);
                }
            }
        });
    }
    int lastPage = 0;
    for (const auto &item : items)
        lastPage = qMax(lastPage, item.page);
    const int numberWidth = ui::interfaceTextWidth(QString::number(lastPage + 1), ui::interfaceFont(c::captionText));
    for (int row = 0; row < buttons.size(); ++row) {
        const int index = page * rows + row;
        const bool shown = row < rows && index < items.size();
        buttons[row]->setVisible(shown);
        removes[row]->setVisible(shown);

        if (!shown)
            continue;
        const auto &item = items[index];
        const int y = listTop() + row * itemHeight;
        auto *button = buttons[row];
        button->setObjectName(QString(annotations ? "rrAnnotation%1" : "rrBookmark%1").arg(index));
        removes[row]->setObjectName(QString(annotations ? "rrAnnotationRemove%1" : "rrBookmarkRemove%1").arg(index));
        removes[row]->setAccessibleName(strings::get(annotations ? "annotations.remove" : "bookmarks.remove"));

        button->setAccessibleName(item.chapter);
        button->setEnabled(item.page >= 0);
        button->setGeometry(0, y, width(), itemHeight);
        auto *itemRow = static_cast<ui::ListRow *>(button);
        itemRow->setActionIcon(ui::ControlIcon::Close);
        itemRow->setDetailFont(excerptFont());
        itemRow->setMetadataFont(ui::interfaceFont(c::captionText));
        itemRow->setTitle(item.chapter);
        itemRow->setDetail(item.excerpt);
        itemRow->setCaption(item.created.isValid() ? QLocale().toString(item.created.toLocalTime().date(), "d MMM yyyy")
                                                   : QString());
        itemRow->setAccessory(
            ui::ListRow::Caption{item.page >= 0 ? (annotations && item.endPage > item.page
                                                       ? QString("%1–%2").arg(item.page + 1).arg(item.endPage + 1)
                                                       : QString::number(item.page + 1))
                                                : QString(),
                                 numberWidth});
    }
}
}
