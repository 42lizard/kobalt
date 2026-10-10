#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/style/theme.h"
#include "navigation.h"
#include "interfaces/reader/ui/controls/text.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "interfaces/reader/ui/style/styles.h"
#include "support/strings.h"
#include <QPainter>
#include <QStackedWidget>

namespace kobalt::reader {
namespace c = ui::metrics::chrome;
namespace {
constexpr ui::Px toolbarBorder = c::heavyRule;
class ToolbarRules final : public QWidget {
public:
    explicit ToolbarRules(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setStyleSheet("background:transparent;");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(QPen(palette().color(QPalette::Mid), ui::metrics::control::border));
        const int status = property("status_height").toInt();
        if (status)
            painter.drawLine(0, status - 1, width(), status - 1);
        painter.drawLine(width() - c::cellWidth - 1, status, width() - c::cellWidth - 1, height());
        painter.drawLine(width() - 2 * c::cellWidth - 1, status, width() - 2 * c::cellWidth - 1, height());
        painter.fillRect(0, height() - toolbarBorder, width(), toolbarBorder, palette().color(QPalette::WindowText));
    }
};
class ReadingRule final : public QWidget {
public:
    explicit ReadingRule(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setStyleSheet("background:transparent;");
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::WindowText));
    }
};
class Tab final : public ui::Button {
public:
    Tab(const QString &label, ui::ControlIcon icon, QWidget *parent) : ui::Button(label, parent), glyph(icon) {
        setCheckable(true);
        setFont(ui::interfaceFont(c::tabText));
        setAccessibleName(label);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    }
    void setSampleFace(const QString &face) {
        const auto next = ui::fontSample(face);
        if (sample == next)
            return;
        sample = next;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const bool active = isChecked() || isDown();
        const auto paper = palette().color(QPalette::Base);
        const auto ink = palette().color(QPalette::Text);
        painter.fillRect(rect(), active ? ink : paper);
        painter.setPen(QPen(palette().color(QPalette::Mid), ui::metrics::control::border));
        painter.drawLine(rect().topRight(), rect().bottomRight());
        if (!isEnabled())
            painter.setOpacity(.45);
        const auto color = active ? paper : ink;
        if (glyph == ui::ControlIcon::Typography) {
            painter.setPen(color);
            painter.setFont(sample);
            ui::drawOpticalText(painter, QRect(0, ui::px(16), width(), c::cellIcon), QStringLiteral("Aa"), true);
        } else
            ui::controlIcon(glyph, color)
                .paint(&painter, QRect((width() - c::cellIcon) / 2, ui::px(16), c::cellIcon, c::cellIcon));
        painter.setPen(color);
        painter.setFont(ui::interfaceFont(c::tabText));
        painter.drawText(QRect(ui::px(2), ui::px(62), width() - ui::px(4), ui::px(36)), Qt::AlignCenter, text());
    }

private:
    ui::ControlIcon glyph;
    QFont sample;
};
}
BottomNavigation::BottomNavigation(QWidget *parent) : ui::Panel(parent, Qt::TopEdge) {
    setObjectName("rrBottomNavigation");
    auto *rows = new QVBoxLayout(this);
    rows->setContentsMargins(0, toolbarBorder, 0, 0);
    rows->setSpacing(0);
    returnRow = new QWidget(this);
    returnRow->setFixedHeight(ui::metrics::links::returnHeight);
    rows->addWidget(returnRow);
    auto *returnLayout = new QHBoxLayout(returnRow);
    returnLayout->setContentsMargins(0, 0, 0, 0);
    returnLayout->setSpacing(0);
    returnButton = new ui::CaptionButton(ui::ControlIcon::Undo, returnRow);
    returnButton->setObjectName("rrNavLinkReturn");
    returnButton->setRule(Qt::BottomEdge);
    returnButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    returnLayout->addWidget(returnButton, 1);
    auto *stay = new ui::CellButton(ui::ControlIcon::Close, returnRow);
    stay->setObjectName("rrNavLinkStay");
    stay->setAccessibleName(strings::get("reader.stay_here"));
    stay->setToolTip(stay->accessibleName());
    stay->setFixedWidth(c::cellWidth);
    stay->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    stay->setRule(ui::Edges(Qt::LeftEdge) | Qt::BottomEdge);
    returnLayout->addWidget(stay);
    connect(stay, &QPushButton::clicked, this, [this] {
        if (stayRequested)
            stayRequested();
    });
    connect(returnButton, &QPushButton::clicked, this, [this] {
        if (returnRequested)
            returnRequested();
    });
    auto *tabRow = new QWidget(this);
    tabRow->setFixedHeight(barHeight - toolbarBorder);
    rows->addWidget(tabRow);
    auto *row = new QHBoxLayout(tabRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    const ui::ControlIcon icons[] = {ui::ControlIcon::Chapters, ui::ControlIcon::Typography, ui::ControlIcon::Layout,
                                     ui::ControlIcon::Light,    ui::ControlIcon::Graph,      ui::ControlIcon::Search};
    const char *names[] = {"Chapters", "Text", "Layout", "Light", "Stats", "Search"};
    const QString labels[] = {strings::get("navigation.chapters"), strings::get("navigation.text"),
                              strings::get("navigation.layout"),   strings::get("navigation.light"),
                              strings::get("navigation.stats"),    strings::get("navigation.search")};
    for (int i = 0; i < 6; ++i) {
        auto *tab = new Tab(labels[i], icons[i], tabRow);
        tab->setObjectName(QString("rrNav%1").arg(names[i]));
        row->addWidget(tab, 1);
        tabs.append(tab);
        connect(tab, &QPushButton::clicked, this, [this, i] {
            if (activated)
                activated(NavigationDestination(i));
        });
    }
    hide();
}
void BottomNavigation::paintEvent(QPaintEvent *event) {
    ui::Panel::paintEvent(event);
    QPainter painter(this);
    if (!panelOpen)
        painter.fillRect(0, 0, width(), toolbarBorder, palette().color(QPalette::WindowText));
}
void BottomNavigation::setPanelOpen(bool open) {
    if (panelOpen == open)
        return;
    panelOpen = open;
    update(QRect(0, 0, width(), toolbarBorder));
}
void BottomNavigation::select(int destination, bool enabled, bool lightAvailable) {
    for (int i = 0; i < tabs.size(); ++i) {
        tabs[i]->setChecked(i == destination);
        tabs[i]->setEnabled(enabled && (i != int(NavigationDestination::Light) || lightAvailable));
    }
}

void BottomNavigation::setTextFace(const QString &face) {
    static_cast<Tab *>(tabs[int(NavigationDestination::Text)])->setSampleFace(face);
}
void BottomNavigation::setLightTab(bool shown) {
    tabs[int(NavigationDestination::Light)]->setVisible(shown);
}
void BottomNavigation::setStatsTab(bool shown) {
    tabs[int(NavigationDestination::Stats)]->setVisible(shown);
}
void BottomNavigation::setReturnLabel(const QString &label, const QString &note) {
    returnRow->setVisible(!label.isEmpty());
    returnButton->setText(label);
    returnButton->setCaption(note);
    returnButton->setAccessibleName(label + " " + note);
}
int BottomNavigation::navigationHeight() const {
    return barHeight + (returnRow->isHidden() ? 0 : ui::metrics::links::returnHeight);
}

Navigation::Navigation(QWidget *parent, const ToolbarComposition &controls)
    : bar(new BottomNavigation(parent)), pageCount(new QLabel(controls.panel)), toolbar(controls.panel),
      toolbarRows(controls.rows), exitAction(controls.exitAction), settings(controls.settings),
      bookmarks(controls.bookmarks), clock(controls.clock), session(controls.session), battery(controls.battery),
      bluetooth(controls.bluetooth) {
    upperEdge = new ReadingRule(parent);
    upperEdge->setObjectName("rrNavigationUpperEdge");
    lowerEdge = new ReadingRule(parent);
    lowerEdge->setObjectName("rrNavigationLowerEdge");
    toolbar->setEdges({});
    toolbarRows->setEnabled(false);
    rules = new ToolbarRules(toolbar);
    rules->lower();
    sessionStyle = session->styleSheet();
    settingsStyle = settings->styleSheet();
    bookmarksStyle = bookmarks->styleSheet();
    ui::readerStyle(session, ui::styles::font(c::countText) + "font-style:italic;");
    ui::readerStyle(settings, "QPushButton{border:none;padding:0;}");
    settings->setFixedSize(c::cellWidth, c::actionHeight);
    if (auto *button = dynamic_cast<ui::Button *>(settings)) {
        settingsPaint = button->paintMode();
        button->setPaintMode(ui::ButtonPaintMode::ToolbarCell);
    }
    ui::readerStyle(bookmarks, "QPushButton{border:none;padding:0;}");
    bookmarks->setFixedSize(c::cellWidth, c::actionHeight);
    if (auto *button = dynamic_cast<ui::Button *>(bookmarks)) {
        bookmarksPaint = button->paintMode();
        button->setPaintMode(ui::ButtonPaintMode::ToolbarCell);
    }
    pageCount->setObjectName("rrMenuPageCount");
    pageCount->setAlignment(Qt::AlignCenter);
    pageCount->setAttribute(Qt::WA_TransparentForMouseEvents);
    ui::readerStyle(pageCount, ui::styles::font(c::countText));
    pageCount->setFont(ui::interfaceFont(c::countText));
    bar->activated = [this](NavigationDestination destination) {
        if (activated)
            activated(destination);
    };
    bar->returnRequested = [this] {
        if (returnRequested)
            returnRequested();
    };
    bar->stayRequested = [this] {
        if (stayRequested)
            stayRequested();
    };
}
Navigation::~Navigation() {
    delete bar;
    delete pageCount;
    delete rules;
    delete upperEdge;
    delete lowerEdge;
    if (auto *button = dynamic_cast<ui::Button *>(settings))
        button->setPaintMode(settingsPaint);
    if (auto *button = dynamic_cast<ui::Button *>(bookmarks))
        button->setPaintMode(bookmarksPaint);
    session->setStyleSheet(sessionStyle);
    settings->setStyleSheet(settingsStyle);
    bookmarks->setStyleSheet(bookmarksStyle);
    settings->setFixedSize(ui::metrics::control::toolbarButtonSize, ui::metrics::control::toolbarButtonSize);
    bookmarks->setFixedSize(ui::metrics::control::toolbarButtonSize, ui::metrics::control::toolbarButtonSize);
    toolbar->setEdges(Qt::BottomEdge);
    toolbarRows->setEnabled(true);
    toolbarRows->invalidate();
}
QRect Navigation::panelBounds(QSize size, int height) const {
    return QRect(0, size.height() - BottomNavigation::barHeight - height, size.width(), height);
}
void Navigation::update(QSize size, const NavigationState &state) {
    state.footer->hide();
    toolbar->setGeometry(0, 0, size.width(), toolbarHeight());
    rules->setProperty("status_height", statusHeight());
    clock->setVisible(statusVisible);
    battery->setVisible(statusVisible);
    session->setVisible(statusVisible && !session->text().isEmpty());
    rules->setGeometry(toolbar->rect());
    rules->show();
    rules->raise();
    clock->setGeometry(c::inset, ui::px(10), ui::px(160), ui::px(40));
    session->setGeometry(ui::px(196), ui::px(10), qMax(0, size.width() - ui::px(392)), ui::px(40));
    battery->move(size.width() - c::inset - battery->width(), ui::px(10));
    if (bluetooth) {
        bluetooth->setVisible(statusVisible && bluetooth->active());
        bluetooth->move(battery->x() - int(ui::metrics::panel::bluetoothGap) - bluetooth->width(), ui::px(10));
        if (bluetooth->active()) {
            const int rightInset = size.width() - bluetooth->x() + int(c::inset);
            const int inset = qMax(ui::px(196), rightInset);
            session->setGeometry(inset, ui::px(10), qMax(0, size.width() - 2 * inset), ui::px(40));
        }
    }
    exitAction->setGeometry(ui::px(24), statusHeight(), exitAction->sizeHint().width(), c::actionHeight);
    settings->setGeometry(size.width() - c::cellWidth, statusHeight(), c::cellWidth, c::actionHeight);
    bookmarks->setGeometry(size.width() - 2 * c::cellWidth, statusHeight(), c::cellWidth, c::actionHeight);
    pageCount->setText(state.pageCount);
    const int labelWidth = size.width() / 3;
    pageCount->setGeometry((size.width() - labelWidth) / 2, exitAction->y(), labelWidth, exitAction->height());
    pageCount->show();
    for (auto *action : state.topActions)
        action->hide();
    bar->setReturnLabel(state.returnLabel, state.returnNote);
    const int height = bar->navigationHeight();
    bar->setGeometry(0, size.height() - height, size.width(), height);
    bar->setVisible(state.visible);
    if (!state.visible) {
        upperEdge->hide();
        lowerEdge->hide();
        return;
    }
    bar->setStatsTab(!state.statsPinned);
    bar->setLightTab(state.lightAvailable);
    for (auto *panel : {state.appearance, state.stats})
        if (panel->isVisible())
            panel->setGeometry(panelBounds(size, panel->height()));
    // Chapters covers the header's action row with its own bar; the status strip stays.
    if (state.chaptersOpen) {
        state.chapters->setGeometry(0, statusHeight(), size.width(), size.height() - height - statusHeight());
        state.chapters->raise();
    }
    bar->select(state.active, state.enabled, state.lightAvailable);
    int top = toolbarHeight(), bottom = bar->y();
    if (state.settings->isVisible())
        top = state.settings->geometry().bottom() + 1;
    for (auto *panel : {state.appearance, state.stats, state.light})
        if (panel->isVisible())
            bottom = qMin(bottom, panel->y());
    bar->setPanelOpen(bottom < bar->y());
    if (!state.chaptersOpen) {
        upperEdge->setGeometry(0, top - ui::px(2), size.width(), ui::px(2));
        upperEdge->show();
        lowerEdge->setGeometry(0, bottom, size.width(), toolbarBorder);
        lowerEdge->show();
    } else {
        upperEdge->hide();
        lowerEdge->hide();
    }
    bar->raise();
    // Keep the same stacking order on replay. Raising each rule in turn
    // invalidates its pixels even when no panel or rule has changed.
    lowerEdge->stackUnder(bar);
    upperEdge->stackUnder(lowerEdge);
}
bool Navigation::contains(QPoint global) const {
    return bar->isVisible() && QRect(bar->mapToGlobal(QPoint()), bar->size()).contains(global);
}
}
