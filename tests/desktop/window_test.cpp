#include <cmath>
#include "variable_fonts.h"
#include "qt_test_shards.h"
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/page_scrubber.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/settings/settings_panel.h"
#include "interfaces/reader/ui/style/theme.h"
#include "platform/desktop/window.h"
#include "platform/android/canvas.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "reading/reading_density.h"
#include "interfaces/reader/views/reading_page.h"
#include "interfaces/reader/settings/settings_panel.h"
#include <QCheckBox>
#include <QAction>
#include <QComboBox>
#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QGraphicsView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QScreen>
#include <QWindow>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

using namespace kobalt;
namespace {
AssetBundle *assets;
FontCopies *copies;
struct Fixture {
    QTemporaryDir directory;
    storage::Paths paths;
    desktop::Window window;
    Fixture()
        : paths{directory.path()},
          window(reportingPaths(), test::variableFontInputs(engineFontInputs(*assets, {}, {}, {})),
                 placeholderFontFamily(*assets, *copies)) {
        window.show();
        window.activateWindow();
    }
    storage::Paths reportingPaths() {
        paths.stateReport = directory.path() + "/state.json";
        return paths;
    }
    ~Fixture() {
        window.close();
        QElapsedTimer elapsed;
        elapsed.start();
        while (!window.idle() && elapsed.elapsed() < 30000)
            QTest::qWait(10);
    }
    QJsonObject state() {
        if (window.readerWindow())
            window.readerWindow()->checkpoint();
        return storage::readJson(paths.stateReport);
    }
    QJsonObject reportedState() const {
        return storage::readJson(paths.stateReport);
    }
    bool canvasClick(QWidget *target, QPoint local) {
        auto *reader = window.readerWindow();
        auto *canvas = window.findChild<QGraphicsView *>();
        if (!reader || !canvas || !target || !target->isVisible() || !target->isEnabled() ||
            !target->rect().contains(local))
            return false;
        const auto point = target->mapTo(reader, local);
        auto *hit = reader->childAt(point);
        if (hit != target && (!hit || !target->isAncestorOf(hit)))
            return false;
        const auto viewportPoint = canvas->mapFromScene(point);
        if (!canvas->viewport()->rect().contains(viewportPoint))
            return false;
        // Keep input on the canvas event path under headless Qt platforms.
        const auto global = canvas->viewport()->mapToGlobal(viewportPoint);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(viewportPoint), QPointF(global), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(viewportPoint), QPointF(global), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &release);
        return true;
    }
    QString book() const {
        return QStringLiteral(KOBALT_FIXTURES) + "/text.epub";
    }
};
}
class DesktopTest : public QObject {
    Q_OBJECT
private slots:
    void dictionariesStayInFillWindowWithoutHover() {
        Fixture f;
        f.window.resize(1000, 1000);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        reader->findChild<ReadingPage *>("rrPage")->tapped(reader->rect().center());
        reader->findChild<QPushButton *>("rrSettings")->click();
        auto *tab = reader->findChild<QPushButton *>("rrSettingsDictionaries");
        QVERIFY(tab->isVisible());
        tab->click();
        QWidget *line = nullptr;
        QTRY_VERIFY((line = reader->findChild<QWidget *>("rrDictionaryRow0")) && line->isVisible());
        ui::ListRow *row = nullptr;
        for (auto *button : line->findChildren<QPushButton *>())
            if (auto *candidate = dynamic_cast<ui::ListRow *>(button))
                row = candidate;
        QVERIFY(row);
        // Isolate the row's painting from the dictionary page's deferred rebuilds.
        QWidget paintHost;
        paintHost.resize(500, 100);
        row->setParent(&paintHost);
        row->setGeometry(paintHost.rect());
        paintHost.show();
        row->show();
        row->setAttribute(Qt::WA_UnderMouse, false);
        const QColor normal = row->grab().toImage().pixelColor(2, 2);
        row->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(row->grab().toImage().pixelColor(2, 2), normal);
        row->setDown(true);
        QVERIFY(row->grab().toImage().pixelColor(2, 2) != normal);
        row->setDown(false);
        auto *pages = reader->findChild<QStackedWidget *>("rrSettingsPages");
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        for (int preset : {1, 2, 0}) {
            picker->setCurrentIndex(preset);
            QCOMPARE(tab->isHidden(), preset != 0);
            if (preset)
                QCOMPARE(pages->currentIndex(), int(reader::DisplaySettings));
        }
        QVERIFY(tab->isVisible());
        tab->click();
        QCOMPARE(pages->currentIndex(), int(reader::DictionariesSettings));
    }
    void pageNavigationFollowsPreviewButtons() {
        Fixture f;
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        picker->setCurrentIndex(2);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        reader->findChild<QPushButton *>("rrSettings")->click();
        reader->findChild<QPushButton *>("rrSettingsControls")->click();
        auto *navigation = reader->findChild<QPushButton *>("rrPageNavigation");
        navigation->click();
        auto *buttons = reader->findChild<QCheckBox *>("rrNavigation_buttons_only");
        QVERIFY(buttons && buttons->isVisible());
        buttons->click();
        QTRY_VERIFY(!reader->findChild<QWidget *>("rrPageNavigationPopup"));
        QCOMPARE(storage::readJson(f.paths.preferences()).value("page_navigation").toString(), QString("buttons_only"));
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        page->tapped(page->rect().center());
        const int before = f.state().value("page").toInt();
        page->swiped(1);
        QTest::qWait(100);
        QCOMPARE(f.state().value("page").toInt(), before);
        for (int preset : {1, 0}) {
            picker->setCurrentIndex(preset);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
            reader->findChild<QPushButton *>("rrSettings")->click();
            reader->findChild<QPushButton *>("rrSettingsControls")->click();
            navigation->click();
            QVERIFY(!reader->findChild<QCheckBox *>("rrNavigation_buttons_only"));
            auto *both = reader->findChild<QCheckBox *>("rrNavigation_taps_and_swipes");
            QVERIFY(both && both->isChecked());
            auto *popup = reader->findChild<QWidget *>("rrPageNavigationPopup");
            QTest::keyClick(popup, Qt::Key_Escape);
            QTRY_VERIFY(!reader->findChild<QWidget *>("rrPageNavigationPopup"));
            page->tapped(page->rect().center());
            const int previous = f.state().value("page").toInt();
            page->swiped(1);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool() && f.state().value("page").toInt() > previous, 10000);
        }
        QCOMPARE(storage::readJson(f.paths.preferences()).value("page_navigation").toString(), QString("buttons_only"));
    }
    void bookmarkTargetCoversRightThird() {
        Fixture f;
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(2);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        auto *toolbar = reader->findChild<QWidget *>("rrControlsTop");
        if (toolbar->isVisible())
            page->tapped(page->rect().center());
        page->swiped(1);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        const auto target = page->bookmarkCorner();
        QCOMPARE(target.width(), qCeil(page->width() * .33));
        QCOMPARE(target.right(), page->width() - 1);
        for (int x : {target.left(), target.center().x(), target.right()}) {
            const bool before = page->bookmarked;
            page->tapped(QPoint(x, target.center().y()));
            QTRY_VERIFY(page->bookmarked != before);
            QVERIFY(!toolbar->isVisible());
        }
    }
    void devicePreviewMenuTapsStayNearCenter() {
        Fixture f;
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        picker->setCurrentIndex(1);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        auto *toolbar = reader->findChild<QWidget *>("rrControlsTop");
        for (int preset : {1, 2}) {
            picker->setCurrentIndex(preset);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
            if (toolbar->isVisible())
                QVERIFY(f.canvasClick(page, page->rect().center()));
            QVERIFY(!toolbar->isVisible());
            const QPoint above(page->width() / 2, page->height() / 5);
            const QPoint below(page->width() / 2, page->height() * 4 / 5);
            for (QPoint point : {QPoint(page->width() / 2, 1), above, below}) {
                QVERIFY(f.canvasClick(page, point));
                QVERIFY(!toolbar->isVisible());
            }
            QVERIFY(f.canvasClick(page, page->rect().center()));
            QVERIFY(toolbar->isVisible());
            auto *scrubber = reader->findChild<ui::PageScrubber *>("rrPageScrubber");
            QVERIFY(scrubber && scrubber->isVisible());
            const int navigationTop =
                page->mapFrom(reader, scrubber->mapTo(reader, scrubber->panelBounds().topLeft())).y();
            // The navigation panel owns its hit area above the tabs.
            const QPoint exposedBelow(page->width() / 2, navigationTop - int(ui::metrics::scrubber::gap));
            for (QPoint point : {above, exposedBelow, QPoint(1, page->height() / 3)}) {
                QVERIFY(f.canvasClick(page, point));
                QVERIFY(toolbar->isVisible());
            }
            auto *hit = reader->childAt(scrubber->mapTo(reader, scrubber->panelBounds().center()));
            QVERIFY(hit == scrubber || scrubber->isAncestorOf(hit));
            QVERIFY(f.canvasClick(page, page->rect().center()));
            QVERIFY(!toolbar->isVisible());
        }
    }
    void heldButtonsShowPressedState() {
        // A held reader button must look pressed in the window, through the canvas.
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        page->tapped(page->rect().center());
        reader->findChild<QPushButton *>("rrNavText")->click();
        auto *font = reader->findChild<QPushButton *>("rrFont");
        QTRY_VERIFY(font && font->isVisible());
        auto *canvas = f.window.findChild<QGraphicsView *>();
        QVERIFY(canvas);
        const QPoint local(font->width() - 12, 6);
        const auto shown = [&] {
            QCoreApplication::processEvents();
            const auto viewportPoint = canvas->mapFromScene(font->mapTo(reader, local));
            const auto image = canvas->viewport()->grab().toImage();
            const qreal dpr = image.devicePixelRatio();
            return image.pixelColor(qRound(viewportPoint.x() * dpr), qRound(viewportPoint.y() * dpr));
        };
        const auto idle = shown();
        const auto viewportPoint = canvas->mapFromScene(font->mapTo(reader, font->rect().center()));
        const auto global = canvas->viewport()->mapToGlobal(viewportPoint);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(viewportPoint), QPointF(global), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &press);
        QVERIFY(font->isDown());
        const auto held = shown();
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(QPoint(-50, -50)), QPointF(global), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &release);
        // The theme's pressed gray, not a platform palette color: setting a
        // style sheet derives a new palette from the platform.
        QVERIFY2(idle == QColor(Qt::white), qPrintable(idle.name()));
        QVERIFY2(held == QColor("#dddddd"), qPrintable(held.name()));
    }
    void devicePreviewTapAbovePanelClosesIt() {
        // In a device preview only the middle of the page opens the menu. A
        // tall panel can cover that middle, so a tap on the visible page
        // above it must still close the panel.
        Fixture f;
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(1);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        auto *menu = reader->findChild<QWidget *>("rrMenu");
        auto *toolbar = reader->findChild<QWidget *>("rrControlsTop");
        page->tapped(page->rect().center());
        QVERIFY(toolbar->isVisible());
        reader->findChild<QPushButton *>("rrNavLayout")->click();
        QTRY_VERIFY(reader->findChild<QWidget *>("rrMargins")->isVisible());
        // The page, toolbar and panel are siblings; compare them in the reader's coordinates.
        const int pageTop = page->mapTo(reader, QPoint()).y();
        const int toolbarBottom = toolbar->mapTo(reader, QPoint()).y() + toolbar->height();
        const int menuTop = menu->mapTo(reader, QPoint()).y();
        QVERIFY(menuTop > toolbarBottom);
        const QPoint above(page->width() / 2, (toolbarBottom + menuTop) / 2 - pageTop);
        page->tapped(above);
        QTRY_VERIFY_WITH_TIMEOUT(!menu->isVisible() && !f.state()["busy"].toBool(), 30000);
    }
    void previewSettingsKeepSavedStatusPreference() {
        Fixture f;
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(1);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(!page->slotContents[0].empty());
        page->tapped(page->rect().center());
        reader->findChild<QPushButton *>("rrSettings")->click();
        auto *nativeTab = reader->findChild<QPushButton *>("rrSettingsUI");
        QVERIFY(nativeTab->isHidden());
        QVERIFY(!nativeTab->isEnabled());
        QVERIFY(reader->findChild<QCheckBox *>("rrHideStatusBar")->isChecked());
    }
    void devicePreviewsHideNativeSettings_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void devicePreviewsHideNativeSettings() {
        QFETCH(bool, dark);
        Fixture f;
        const QJsonObject preferences = {
            {"dark_mode", dark}, {"native_hide_status_bar", false}, {"native_hide_device_options", false}};
        QVERIFY(storage::writeJson(f.paths.preferences(), preferences));
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        const auto savedPreferences = storage::readJson(f.paths.preferences());
        auto *reader = f.window.readerWindow();
        reader->findChild<ReadingPage *>("rrPage")->tapped(reader->rect().center());
        reader->findChild<QPushButton *>("rrSettings")->click();
        auto *nativeTab = reader->findChild<QPushButton *>("rrSettingsUI");
        auto *pages = reader->findChild<QStackedWidget *>("rrSettingsPages");
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        QVERIFY(nativeTab && pages && picker && resolution);
        QVERIFY(nativeTab->isVisible() && nativeTab->isEnabled());
        nativeTab->click();
        QCOMPARE(pages->currentIndex(), int(reader::NativeUiSettings));
        for (int preset : {1, 0, 2, 0}) {
            picker->setCurrentIndex(preset);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
            QCOMPARE(nativeTab->isHidden(), preset != 0);
            QCOMPARE(nativeTab->isEnabled(), preset == 0);
            QCOMPARE(pages->currentIndex(), int(reader::DisplaySettings));
            if (!preset) {
                nativeTab->click();
                QCOMPARE(pages->currentIndex(), int(reader::NativeUiSettings));
                QVERIFY(reader->findChild<QCheckBox *>("rrHideStatusBar")->isVisible());
            }
            for (int dpi : {212, 300}) {
                resolution->setCurrentIndex(resolution->findData(dpi));
                QCoreApplication::processEvents();
                QVERIFY(f.window.grab().save(QString("native-desktop-settings-tabs-%1-%2-%3.png")
                                                 .arg(preset)
                                                 .arg(dark ? "dark" : "light")
                                                 .arg(dpi)));
            }
        }
        QVERIFY(!reader->findChild<QCheckBox *>("rrHideStatusBar")->isChecked());
        QVERIFY(!reader->findChild<QCheckBox *>("rrHideDeviceOptions")->isChecked());
        QCOMPARE(storage::readJson(f.paths.preferences()), savedPreferences);
    }
    void bookmarksFollowStatusAndPreviewChanges() {
        for (bool hidden : {true, false}) {
            Fixture f;
            QVERIFY(storage::writeJson(f.paths.preferences(), {{"native_hide_status_bar", hidden}}));
            QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
            f.window.openBook(f.book());
            QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
            auto *reader = f.window.readerWindow();
            auto *page = reader->findChild<ReadingPage *>("rrPage");
            page->tapped(page->rect().center());
            reader->findChild<QPushButton *>("rrBookmarks")->click();
            auto *list = reader->findChild<QWidget *>("rrBookmarkList");
            auto *title = reader->findChild<QLabel *>("rrBookmarkListTitle");
            auto *rule = reader->findChild<QWidget *>("rrNavigationUpperEdge");
            auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
            for (int preset : {0, 1, 2, 0}) {
                picker->setCurrentIndex(preset);
                const int top = preset || !hidden ? int(ui::metrics::chrome::statusHeight) : 0;
                QTRY_COMPARE(list->y(), top);
                QVERIFY(list->isVisible());
                QVERIFY(rule->isHidden());
                // The time shows whether or not the status bar is hidden.
                QVERIFY(!page->slotContents[0].empty());
                QVERIFY(!title->visibleRegion().isEmpty());
                if (preset)
                    QCOMPARE(reader->findChild<QLabel *>("rrMenuClock")->text(), QString("09:00"));
                // The panel's close control must receive input above the navigation rules.
                auto *close = reader->findChild<QPushButton *>("rrBookmarkListClose");
                const QPoint center = close->mapTo(reader, close->rect().center());
                auto *hit = reader->childAt(center);
                QVERIFY(hit == close || close->isAncestorOf(hit));
            }
            QCOMPARE(storage::readJson(f.paths.preferences())["native_hide_status_bar"].toBool(), hidden);
        }
    }
    void trackpadTouchDoesNotEmulateMouse() {
        class Surface final : public QWidget {
        public:
            int presses = 0, releases = 0, moves = 0, cancels = 0;
            bool event(QEvent *event) override {
                if (event->type() == QEvent::MouseButtonPress)
                    ++presses;
                if (event->type() == QEvent::MouseButtonRelease)
                    ++releases;
                if (event->type() == QEvent::MouseMove)
                    ++moves;
                if (event->type() == QEvent::TouchCancel)
                    ++cancels;
                return QWidget::event(event);
            }
        };
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(400, 300);
        auto *surface = new Surface;
        surface->resize(1000, 1000);
        canvas.setContent(surface, true);
        canvas.setSceneRect(0, 0, 1000, 1000);
        canvas.setTransform(QTransform::fromScale(0.3, 0.3));
        canvas.show();
        auto *trackpad = QTest::createTouchDevice(QInputDevice::DeviceType::TouchPad);
        QTouchEvent begin(QEvent::TouchBegin, trackpad, Qt::NoModifier,
                          {QEventPoint(0, QEventPoint::Pressed, QPointF(), QPointF())});
        QApplication::sendEvent(canvas.viewport(), &begin);
        QCOMPARE(surface->presses, 0);
        QTouchEvent update(QEvent::TouchUpdate, trackpad, Qt::NoModifier,
                           {QEventPoint(0, QEventPoint::Updated, QPointF(50, 50), QPointF(50, 50))});
        QApplication::sendEvent(canvas.viewport(), &update);
        QTouchEvent cancel(QEvent::TouchCancel, trackpad);
        QApplication::sendEvent(canvas.viewport(), &cancel);
        QTouchEvent end(QEvent::TouchEnd, trackpad, Qt::NoModifier,
                        {QEventPoint(0, QEventPoint::Released, QPointF(50, 50), QPointF(50, 50))});
        QApplication::sendEvent(canvas.viewport(), &end);
        QCOMPARE(surface->moves, 0);
        QCOMPARE(surface->cancels, 0);
        QCOMPARE(surface->releases, 0);
        // A touchscreen still needs the canvas adapter on Android.
        auto *screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        QTouchEvent screenBegin(QEvent::TouchBegin, screen, Qt::NoModifier,
                                {QEventPoint(0, QEventPoint::Pressed, QPointF(), QPointF())});
        QApplication::sendEvent(canvas.viewport(), &screenBegin);
        QCOMPARE(surface->presses, 1);
        QTouchEvent screenEnd(QEvent::TouchEnd, screen, Qt::NoModifier,
                              {QEventPoint(0, QEventPoint::Released, QPointF(), QPointF())});
        QApplication::sendEvent(canvas.viewport(), &screenEnd);
        QCOMPARE(surface->releases, 1);
    }
    void pageTapsAfterClosingText_data() {
        QTest::addColumn<int>("preset");
        QTest::newRow("six-inch") << 1;
        QTest::newRow("seven-inch") << 2;
    }
    void pageTapsAfterClosingText() {
        QFETCH(int, preset);
        Fixture f;
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(page);
        auto click = [&](QWidget *target, QPoint position) {
            auto *canvas = f.window.findChild<QGraphicsView *>();
            const auto viewportPoint = canvas->mapFromScene(target->mapTo(reader, position));
            QTest::mouseClick(f.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                              canvas->viewport()->mapTo(&f.window, viewportPoint));
        };
        click(page, page->rect().center());
        auto *text = reader->findChild<QPushButton *>("rrNavText");
        QVERIFY(text);
        QTRY_VERIFY(text->isVisible());
        click(text, text->rect().center());
        QTest::qWait(200);
        click(page, QPoint(page->width() / 2, page->height() / 3));
        QTRY_VERIFY(!text->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        const auto before = f.state().value("page").toInt();
        click(page, QPoint(page->width() * 5 / 6, page->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(f.state().value("page").toInt() > before, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        click(page, QPoint(page->width() / 6, page->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(f.state().value("page").toInt(), before, 10000);
    }
    void screenshotExport_data() {
        QTest::addColumn<int>("preset");
        QTest::addColumn<bool>("dark");
        for (int preset : {0, 1, 2}) {
            for (bool dark : {false, true}) {
                QTest::newRow(qPrintable(QString("%1-%2").arg(preset).arg(dark ? "dark" : "light"))) << preset << dark;
            }
        }
    }
    void screenshotExport() {
        QFETCH(int, preset);
        QFETCH(bool, dark);
        Fixture f;
        QVERIFY(storage::writeJson(f.paths.preferences(), {{"dark_mode", dark}}));
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        QTemporaryDir output;
        QVERIFY(output.isValid());
        QTest::qWait(150);
        QString error = "old error";
        const QString startup = f.window.saveScreenshot(output.path(), &error);
        QVERIFY2(!startup.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(!QImage(startup).isNull());
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        page->tapped(page->rect().center());
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        auto *stage = f.window.centralWidget();
        auto *stack = f.window.findChild<QStackedWidget *>();
        auto *frame = f.window.findChild<QWidget *>("desktopDeviceFrame");
        const auto position = f.state().value("position").toString();
        const auto pages = f.state().value("pages").toInt();
        const qreal dpr = stage->devicePixelRatioF();
        const auto physicalRect = [dpr](QRect rect) {
            return QRect(qRound(rect.x() * dpr), qRound(rect.y() * dpr), qRound(rect.width() * dpr),
                         qRound(rect.height() * dpr));
        };
        QString previous = startup;
        for (int dpi : {212, 300, 0}) {
            resolution->setCurrentIndex(resolution->findData(dpi));
            QCoreApplication::processEvents();
            QImage visible = stage->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            // PNG retains physical pixels, not Qt's in-memory display scale.
            visible.setDevicePixelRatio(1);
            const QString path = f.window.saveScreenshot(output.path(), &error);
            QVERIFY2(!path.isEmpty(), qPrintable(error));
            QVERIFY(error.isEmpty());
            QVERIFY(path != previous);
            QVERIFY(QFile::exists(previous));
            previous = path;
            const QImage exported = QImage(path).convertToFormat(QImage::Format_ARGB32);
            QCOMPARE(exported.size(), QSize(qRound(stage->width() * dpr), qRound(stage->height() * dpr)));
            QCOMPARE(exported.copy(physicalRect(stack->geometry())), visible.copy(physicalRect(stack->geometry())));
            QCOMPARE(exported.pixelColor(0, 0).alpha(), preset ? 0 : 255);
            if (preset) {
                const QRect body = physicalRect(frame->geometry());
                QCOMPARE(exported.pixelColor(body.topLeft()).alpha(), 0);
                const QPoint bezel(body.left() + body.width() / 25, body.center().y());
                QCOMPARE(exported.pixelColor(bezel).alpha(), 255);
                QVERIFY(exported.pixelColor(bezel).lightness() < 80);
                // White book paper and black night-mode paper both remain opaque.
                const QRect screen = physicalRect(stack->geometry());
                QCOMPARE(exported.pixelColor(screen.topLeft() + QPoint(2, 2)).alpha(), 255);
            }
            QVERIFY(exported.save(QString("native-desktop-screenshot-%1-%2-%3-%4.png")
                                      .arg(preset)
                                      .arg(dark ? "dark" : "light")
                                      .arg(dpi)
                                      .arg(dpr)));
            QCOMPARE(f.state().value("position").toString(), position);
            QCOMPARE(f.state().value("pages").toInt(), pages);
        }
        QCOMPARE(QDir(output.path()).entryList({"*.png"}, QDir::Files).size(), 4);
        QVERIFY(f.window.saveScreenshot(output.path() + "/missing", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!QDir(output.path() + "/missing").exists());
        QVERIFY(f.window.saveScreenshot({}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void toolbarLayout() {
        Fixture f;
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        QVERIFY(picker);
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        QVERIFY(resolution);
        auto *pickers = f.window.findChild<QWidget *>("desktopPreviewPickers");
        QVERIFY(pickers);
        auto *screenshot = f.window.findChild<QPushButton *>("desktopScreenshot");
        QVERIFY(screenshot);
        QCOMPARE(screenshot->text(), QString("Screenshot"));
        auto *logo = f.window.findChild<QWidget *>("desktopLogo");
        QVERIFY(logo);
        auto *version = f.window.findChild<QLabel *>("desktopVersion");
        QVERIFY(version);
        QCOMPARE(version->text(), QStringLiteral(KOBALT_VERSION));
        QVERIFY(QFile::exists(":/desktop/kobalt-android.svg"));
        for (const int width : {420, 640, 920}) {
            f.window.resize(width, 800);
            QTest::qWait(30);
            const auto center = pickers->mapTo(&f.window, pickers->rect().center()).x();
            QVERIFY(qAbs(center - f.window.rect().center().x()) <= 2);
            QVERIFY(logo->mapTo(&f.window, QPoint()).x() < picker->mapTo(&f.window, QPoint()).x());
            QVERIFY(picker->geometry().right() < resolution->geometry().left());
            QVERIFY(resolution->geometry().right() < screenshot->geometry().left());
            QVERIFY(pickers->rect().contains(screenshot->geometry()));
            QVERIFY2(screenshot->width() >= screenshot->sizeHint().width(),
                     qPrintable(QString("Window %1, button %2, preferred %3")
                                    .arg(f.window.width())
                                    .arg(screenshot->width())
                                    .arg(screenshot->sizeHint().width())));
            QVERIFY(version->mapTo(&f.window, QPoint()).x() >
                    screenshot->mapTo(&f.window, screenshot->rect().topRight()).x());
        }
    }
    void previewMenusMatchToolbarChoices() {
        Fixture f;
        auto *device = f.window.findChild<QComboBox *>("desktopScreenPicker");
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        auto *deviceMenu = f.window.findChild<QMenu *>("desktopDeviceMenu");
        auto *resolutionMenu = f.window.findChild<QMenu *>("desktopResolutionMenu");
        QVERIFY(device && resolution && deviceMenu && resolutionMenu);
        const auto verify = [](QComboBox *picker, QMenu *menu) {
            QCOMPARE(menu->actions().size(), picker->count());
            for (int index = 0; index < picker->count(); ++index) {
                picker->setCurrentIndex(index);
                for (int item = 0; item < picker->count(); ++item) {
                    QCOMPARE(menu->actions().at(item)->text(), picker->itemText(item));
                    QCOMPARE(menu->actions().at(item)->isChecked(), item == index);
                }
            }
            for (int index = picker->count() - 1; index >= 0; --index) {
                menu->actions().at(index)->trigger();
                QCOMPARE(picker->currentIndex(), index);
                QVERIFY(menu->actions().at(index)->isChecked());
            }
        };
        verify(device, deviceMenu);
        verify(resolution, resolutionMenu);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        for (auto *menu : {deviceMenu, resolutionMenu})
            for (auto *action : menu->actions())
                QVERIFY(!action->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        for (auto *menu : {deviceMenu, resolutionMenu})
            for (auto *action : menu->actions())
                QVERIFY(action->isEnabled());
    }
    void hiddenToolbarRestoresReaderGeometry_data() {
        QTest::addColumn<int>("preset");
        QTest::addColumn<bool>("dark");
        for (int preset : {0, 1, 2}) {
            for (bool dark : {false, true}) {
                const auto name = QString("%1-%2").arg(preset).arg(dark ? "dark" : "light");
                QTest::newRow(qPrintable(name)) << preset << dark;
            }
        }
    }
    void hiddenToolbarRestoresReaderGeometry() {
        QFETCH(int, preset);
        QFETCH(bool, dark);
        Fixture f;
        QVERIFY(storage::writeJson(f.paths.preferences(), {{"dark_mode", dark}}));
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        resolution->setCurrentIndex(resolution->findData(0));
        auto *toolbar = f.window.findChild<QToolBar *>("desktopPreviewToolbar");
        auto *showToolbar = f.window.findChild<QAction *>("desktopShowToolbar");
        auto *view = f.window.findChild<QMenu *>("desktopViewMenu");
        QVERIFY(toolbar && showToolbar && view);
        QVERIFY(view->actions().contains(showToolbar));
        auto *context = f.window.createPopupMenu();
        QVERIFY(context && context->actions().contains(showToolbar));
        delete context;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *canvas = f.window.findChild<android::WidgetCanvas *>();
        const auto size = reader->size();
        const auto viewport = canvas->viewport()->size();
        const auto viewportGeometry = [canvas, &f] {
            return QRect(canvas->viewport()->mapTo(&f.window, QPoint()), canvas->viewport()->size());
        };
        const auto originalGeometry = viewportGeometry();
        const auto position = f.state().value("position").toString();
        const auto pages = f.state().value("pages").toInt();
        const auto prefix = QString("native-desktop-toolbar-%1-%2-%3")
                                .arg(preset)
                                .arg(dark ? "dark" : "light")
                                .arg(canvas->devicePixelRatioF());
        QVERIFY(f.window.grab().save(prefix + "-shown.png"));
        // The context menu uses the toolbar's own action too.
        toolbar->hide();
        QTRY_VERIFY(!showToolbar->isChecked());
        QTRY_VERIFY(viewportGeometry() != originalGeometry);
        if (preset == 0)
            QTRY_COMPARE(reader->size(), f.window.centralWidget()->size() * 2);
        else {
            auto *frame = f.window.findChild<QWidget *>("desktopDeviceFrame");
            QVERIFY(frame);
            QTRY_VERIFY(qAbs(frame->geometry().center().y() - f.window.centralWidget()->rect().center().y()) <= 1);
            QCOMPARE(reader->size(), size);
            QCOMPARE(f.state().value("pages").toInt(), pages);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(f.state().value("position").toString(), position);
        QVERIFY(f.window.grab().save(prefix + "-hidden.png"));
        showToolbar->trigger();
        QTRY_VERIFY(toolbar->isVisible() && showToolbar->isChecked());
        QTRY_COMPARE(canvas->viewport()->size(), viewport);
        QTRY_COMPARE(viewportGeometry(), originalGeometry);
        QTRY_COMPARE(reader->size(), size);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(f.state().value("position").toString(), position);
        QVERIFY(f.window.grab().save(prefix + "-restored.png"));
        showToolbar->trigger();
        QTRY_VERIFY(!toolbar->isVisible());
        auto *deviceMenu = f.window.findChild<QMenu *>("desktopDeviceMenu");
        auto *resolutionMenu = f.window.findChild<QMenu *>("desktopResolutionMenu");
        deviceMenu->actions().at(preset == 1 ? 2 : 1)->trigger();
        resolutionMenu->actions().at(0)->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(resolution->currentData().toInt(), 212);
        QVERIFY(!toolbar->isVisible());
        showToolbar->trigger();
        QTRY_VERIFY(toolbar->isVisible());
    }
    void resizedDeviceControls_data() {
        QTest::addColumn<int>("preset");
        QTest::newRow("six-inch") << 1;
        QTest::newRow("seven-inch") << 2;
    }
    void resizedDeviceControls() {
        QFETCH(int, preset);
        Fixture f;
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        for (const auto size : {QSize(920, 600), QSize(420, 500), QSize(640, 800)}) {
            f.window.resize(size);
            QTest::qWait(200);
            auto *reader = f.window.readerWindow();
            auto *page = reader->findChild<ReadingPage *>("rrPage");
            QVERIFY(page);
            QVERIFY(f.canvasClick(page, page->rect().center()));
            auto *settings = reader->findChild<QPushButton *>("rrSettings");
            QVERIFY(settings);
            QTRY_VERIFY(settings->isVisible());
            QVERIFY(f.canvasClick(settings, settings->rect().center()));
            QVERIFY(reader->findChild<QPushButton *>("rrSettingsUI")->isHidden());
            auto *tab = reader->findChild<QPushButton *>("rrSettingsControls");
            QVERIFY(tab);
            QTRY_VERIFY(tab->isVisible());
            QVERIFY(f.canvasClick(tab, tab->rect().center()));
            auto *toggle = reader->findChild<QCheckBox *>("rrShowNavigationBar");
            QVERIFY(toggle);
            QTRY_VERIFY(toggle->isVisible());
            const bool before = toggle->isChecked();
            QVERIFY(f.canvasClick(toggle, toggle->rect().center()));
            QTRY_COMPARE(toggle->isChecked(), !before);
            QVERIFY(f.canvasClick(settings, settings->rect().center()));
            QTRY_VERIFY(!reader->findChild<QWidget *>("rrGeneralSettings")->isVisible());
            if (settings->isVisible())
                QVERIFY(f.canvasClick(page, page->rect().center()));
        }
    }
    void fillWindowRulesUseDisplayPixels() {
        class Surface final : public QWidget {
        public:
            qreal paintedScale = 0;
            void paintEvent(QPaintEvent *) override {
                QPainter painter(this);
                paintedScale = painter.deviceTransform().m11();
                painter.fillRect(rect(), Qt::white);
                for (int thickness : {2, 3}) {
                    for (int offset = 0; offset < 4; ++offset) {
                        ui::paintEdges(painter, QRect(20, 40 + thickness * 80 + offset * 21, 600, 10), Qt::TopEdge,
                                       Qt::black, thickness);
                    }
                }
            }
        };
        android::WidgetCanvas canvas(nullptr);
        auto *surface = new Surface;
        surface->resize(800, 400);
        canvas.setContent(surface, false);
        canvas.setSceneRect(0, 0, 800, 400);
        canvas.resize(400, 200);
        canvas.setTransform(QTransform::fromScale(.5, .5));
        canvas.show();
        QCoreApplication::processEvents();
        const auto capture = canvas.viewport()->grab();
        const auto image = capture.toImage();
        const qreal dpr = capture.devicePixelRatioF();
        QVERIFY(qAbs(surface->paintedScale - .5 * dpr) < .01);
        for (int thickness : {2, 3}) {
            for (int offset = 0; offset < 4; ++offset) {
                const QPoint point = canvas.mapFromScene(QPointF(100, 40 + thickness * 80 + offset * 21)) * dpr;
                int black = 0;
                for (int dy = -3; dy <= 5; ++dy) {
                    const auto pixel = image.pixelColor(point.x(), point.y() + dy);
                    QVERIFY(pixel == QColor(Qt::black) || pixel == QColor(Qt::white));
                    black += pixel == QColor(Qt::black);
                }
                QCOMPARE(black, qMax(1, qRound(thickness * .5 * dpr)));
            }
        }
        canvas.setNativePixels(true);
        QCoreApplication::processEvents();
        canvas.viewport()->grab();
        QCOMPARE(surface->paintedScale, 1.0);
        canvas.setNativePixels(false);
        QCoreApplication::processEvents();
        canvas.viewport()->grab();
        QVERIFY(qAbs(surface->paintedScale - .5 * dpr) < .01);
    }
    void fillWindowNavigationAtDisplayScale_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void fillWindowNavigationAtDisplayScale() {
        QFETCH(bool, dark);
        Fixture f;
        QVERIFY(storage::writeJson(f.paths.preferences(), {{"dark_mode", dark}}));
        f.window.resize(920, 600);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(f.canvasClick(page, page->rect().center()));
        auto *scrubber = reader->findChild<ui::PageScrubber *>("rrPageScrubber");
        QTRY_VERIFY(scrubber && scrubber->isVisible());
        auto *canvas = f.window.findChild<android::WidgetCanvas *>();
        QVERIFY(canvas);
        const auto position = f.state().value("position").toString();
        const auto size = reader->size();
        const auto prefix =
            QString("native-desktop-fill-%1-%2").arg(dark ? "dark" : "light").arg(canvas->devicePixelRatioF());
        canvas->setNativePixels(true);
        QCoreApplication::processEvents();
        QVERIFY(f.window.grab().save(prefix + "-before.png"));
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        resolution->setCurrentIndex(resolution->findData(0));
        QCoreApplication::processEvents();
        QVERIFY(f.window.grab().save(prefix + "-after.png"));
        QCOMPARE(reader->size(), size);
        QCOMPARE(f.state().value("position").toString(), position);
        auto *next = reader->findChild<ui::Button *>("rrNavigationNext");
        QCOMPARE(next->iconConfiguration()->glyph, ui::ControlIcon::NextChapter);
        QVERIFY(f.canvasClick(next, next->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(scrubber->previewsReady(), 30000);
        QVERIFY(f.window.grab().save(prefix + "-preview.png"));
        QCOMPARE(f.state().value("position").toString(), position);
    }
    void previewRendersInNativePixels() {
        class Surface final : public QWidget {
        public:
            qreal paintedScale = 0;
            QColor ink = Qt::black;
            void paintEvent(QPaintEvent *) override {
                QPainter painter(this);
                paintedScale = painter.deviceTransform().m11();
                painter.fillRect(rect(), Qt::white);
                painter.fillRect(QRect(100, 100, 100, 100), ink);
            }
        };
        android::WidgetCanvas canvas(nullptr);
        auto *surface = new Surface;
        surface->resize(1000, 1000);
        canvas.setContent(surface, true);
        canvas.setSceneRect(0, 0, 1000, 1000);
        canvas.resize(500, 500);
        canvas.setTransform(QTransform::fromScale(0.5, 0.5));
        canvas.show();
        QCoreApplication::processEvents();
        auto sample = [&] {
            const auto point = canvas.mapFromScene(QPointF(150, 150)) * canvas.devicePixelRatioF();
            return canvas.viewport()->grab().toImage().pixelColor(point);
        };
        QCOMPARE(sample(), QColor(Qt::black));
        QCOMPARE(surface->paintedScale, 1.0);
        surface->ink = Qt::red;
        surface->update(QRect(100, 100, 100, 100));
        QTRY_COMPARE(sample(), QColor(Qt::red));
        canvas.resize(250, 250);
        canvas.setTransform(QTransform::fromScale(0.25, 0.25));
        QCoreApplication::processEvents();
        QCOMPARE(sample(), QColor(Qt::red));
        QCOMPARE(surface->paintedScale, 1.0);
    }
    void reducedResolutionKeepsFilteredDamage() {
        class Surface final : public QWidget {
        public:
            QColor ink = Qt::black;
            void paintEvent(QPaintEvent *) override {
                QPainter painter(this);
                painter.fillRect(rect(), Qt::white);
                painter.fillRect(QRect(101, 101, 100, 100), ink);
            }
        };
        android::WidgetCanvas canvas(nullptr);
        auto *surface = new Surface;
        surface->resize(500, 500);
        canvas.setContent(surface, true, 212.0 / 300);
        canvas.setSceneRect(0, 0, 500, 500);
        canvas.resize(600, 600);
        canvas.show();
        QCoreApplication::processEvents();
        auto capture = [&] {
            return canvas.viewport()->grab().toImage();
        };
        capture();
        QCOMPARE(canvas.rasterSize(), QSize(353, 353));
        const auto point = canvas.mapFromScene(QPointF(150, 150)) * canvas.devicePixelRatioF();
        QCOMPARE(capture().pixelColor(point), QColor(Qt::black));
        const auto edge = canvas.mapFromScene(QPointF(101, 150)) * canvas.devicePixelRatioF();
        bool filtered = false;
        const auto image = capture();
        for (int dx = -4; dx <= 4; ++dx) {
            const auto color = image.pixelColor(edge.x() + dx, edge.y());
            filtered |= color.red() > 0 && color.red() < 255;
        }
        QVERIFY(filtered);
        surface->ink = Qt::red;
        surface->update(QRect(101, 101, 100, 100));
        QTRY_COMPARE(capture().pixelColor(point), QColor(Qt::red));
        canvas.setRasterScale(1);
        capture();
        QCOMPARE(canvas.rasterSize(), surface->size());
        QCOMPARE(capture().pixelColor(point), QColor(Qt::red));
        canvas.setNativePixels(false);
        capture();
        QVERIFY(canvas.rasterSize().isEmpty());
    }
    void previewThumbnailBordersSurviveScaling_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void previewThumbnailBordersSurviveScaling() {
        QFETCH(bool, dark);
        const auto oldDensity = ui::density();
        ui::setDensity(300);
        struct RestoreDensity {
            int value;
            ~RestoreDensity() {
                ui::setDensity(value);
            }
        } restore{oldDensity};
        android::WidgetCanvas canvas(nullptr);
        auto *surface = new QWidget;
        surface->resize(1280, 1440);
        surface->setAutoFillBackground(true);
        auto *scrubber = new ui::PageScrubber(surface);
        scrubber->setGeometry(surface->rect());
        scrubber->setBook(20, 100, {{0, "First"}, {50, "Second"}}, {3, 4});
        const QColor paper = dark ? Qt::black : Qt::white, ink = dark ? Qt::white : Qt::black;
        auto palette = surface->palette();
        palette.setColor(QPalette::Window, paper);
        palette.setColor(QPalette::Base, paper);
        palette.setColor(QPalette::Text, ink);
        palette.setColor(QPalette::WindowText, ink);
        surface->setPalette(palette);
        for (auto *child : surface->findChildren<QWidget *>())
            child->setPalette(palette);
        scrubber->previewRequested = [scrubber, paper](int page, QSize size) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(paper);
            scrubber->setPreview(page, image);
        };
        canvas.setContent(surface, false);
        canvas.setSceneRect(0, 0, 1280, 1600);
        canvas.resize(640, 800);
        canvas.setTransform(QTransform::fromScale(.5, .5));
        canvas.show();
        scrubber->show();
        scrubber->findChild<QPushButton *>("rrNavigationNext")->click();
        QTRY_VERIFY(scrubber->previewsReady());
        for (int offset = 0; offset < 8; ++offset) {
            surface->resize(1280, 1440 + offset);
            scrubber->setGeometry(surface->rect());
            QCoreApplication::processEvents();
            const auto capture = canvas.viewport()->grab();
            const auto image = capture.toImage();
            if (offset == 0)
                QVERIFY(capture.save(QString("native-thumbnail-borders-%1-%2.png")
                                         .arg(dark ? "dark" : "light")
                                         .arg(canvas.devicePixelRatioF())));
            for (const auto *name : {"rrPreviewPreviousPage", "rrPreviewNextPage"}) {
                auto *thumbnail = scrubber->findChild<QWidget *>(name);
                QVERIFY(thumbnail && thumbnail->isVisible());
                const auto topLeft = canvas.viewportTransform().map(QPointF(thumbnail->mapTo(surface, QPoint())));
                const qreal scale = .5 * canvas.devicePixelRatioF();
                const QPoint first(qRound(topLeft.x() * canvas.devicePixelRatioF()),
                                   qRound(topLeft.y() * canvas.devicePixelRatioF()));
                const QPoint last(qRound(topLeft.x() * canvas.devicePixelRatioF() + thumbnail->width() * scale) - 1,
                                  qRound(topLeft.y() * canvas.devicePixelRatioF() + thumbnail->height() * scale) - 1);
                const QRect bounds(first, last);
                const auto visible = bounds.intersected(image.rect());
                QVERIFY(visible.width() > 10);
                const int x = visible.center().x();
                QVERIFY2(image.pixelColor(x, bounds.top()) == ink,
                         qPrintable(QString("%1 top border missing at offset %2").arg(name).arg(offset)));
                QVERIFY2(image.pixelColor(x, bounds.bottom()) == ink,
                         qPrintable(QString("%1 bottom border missing at offset %2").arg(name).arg(offset)));
            }
        }
    }
    void renderingResolutionPreservesLayout_data() {
        QTest::addColumn<int>("preset");
        QTest::addColumn<bool>("dark");
        for (int preset : {0, 1, 2}) {
            for (bool dark : {false, true}) {
                const auto name = QString("%1-%2").arg(preset).arg(dark ? "dark" : "light");
                QTest::newRow(qPrintable(name)) << preset << dark;
            }
        }
    }
    void renderingResolutionPreservesLayout() {
        QFETCH(int, preset);
        QFETCH(bool, dark);
        Fixture f;
        QVERIFY(storage::writeJson(f.paths.preferences(), {{"dark_mode", dark}}));
        f.window.resize(920, 800);
        f.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        auto *resolution = f.window.findChild<QComboBox *>("desktopResolutionPicker");
        QVERIFY(resolution);
        QCOMPARE(resolution->count(), 3);
        QCOMPARE(resolution->currentData().toInt(), 0);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *canvas = f.window.findChild<android::WidgetCanvas *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->rasterSize(), QSize(0, 0));
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(f.canvasClick(page, page->rect().center()));
        auto *scrubber = reader->findChild<ui::PageScrubber *>("rrPageScrubber");
        QTRY_VERIFY(scrubber && scrubber->isVisible());
        const auto size = reader->size();
        const auto transform = canvas->transform();
        const auto position = f.state().value("position").toString();
        const auto pages = f.state().value("pages").toInt();
        for (int dpi : {212, 0, 300}) {
            resolution->setCurrentIndex(resolution->findData(dpi));
            QCoreApplication::processEvents();
            const auto name = QString("native-desktop-resolution-%1-%2-%3-%4.png")
                                  .arg(preset)
                                  .arg(dark ? "dark" : "light")
                                  .arg(dpi)
                                  .arg(canvas->devicePixelRatioF());
            QVERIFY(f.window.grab().save(name));
            QCOMPARE(f.window.readerWindow(), reader);
            QCOMPARE(reader->size(), size);
            QCOMPARE(canvas->transform(), transform);
            QCOMPARE(f.state().value("pages").toInt(), pages);
            QCOMPARE(f.state().value("position").toString(), position);
            QCOMPARE(opened.count(), 1);
            QVERIFY(scrubber->isVisible());
            const QSize expected =
                dpi ? QSize(qRound(size.width() * dpi / 300.0), qRound(size.height() * dpi / 300.0)) : QSize(0, 0);
            QCOMPARE(canvas->rasterSize(), expected);
            QCOMPARE(readingPpi.load(), 300);
        }
        resolution->setCurrentIndex(resolution->findData(0));
        auto *next = reader->findChild<ui::Button *>("rrNavigationNext");
        QVERIFY(f.canvasClick(next, next->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(scrubber->previewsReady(), 30000);
        QCOMPARE(f.state().value("position").toString(), position);
    }
    void deviceScreensKeepResolutionAndPosition() {
        Fixture f;
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        QVERIFY(picker);
        picker->setCurrentIndex(1);
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(f.window.readerWindow()->size(), QSize(1072, 1448));
        QCOMPARE(readingPpi.load(), 300);
        QCOMPARE(ui::density(), 300);
        const auto firstPage = f.state().value("page").toInt();
        picker->setFocus();
        QTest::keyClick(&f.window, Qt::Key_Right);
        QCOMPARE(f.state().value("page").toInt(), firstPage);
        auto *page = f.window.readerWindow()->findChild<ReadingPage *>("rrPage");
        QVERIFY(page);
        QVERIFY(f.canvasClick(page, QPoint(page->width() * 5 / 6, page->height() / 2)));
        QTRY_VERIFY_WITH_TIMEOUT(f.state().value("page").toInt() > firstPage, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        const auto position = f.state().value("position").toString();
        const auto pages = f.state().value("pages").toInt();
        f.window.resize(920, 600);
        QTest::qWait(200);
        QCOMPARE(f.window.readerWindow()->size(), QSize(1072, 1448));
        QCOMPARE(f.state().value("pages").toInt(), pages);
        auto *canvas = f.window.findChild<QGraphicsView *>();
        QVERIFY(canvas);
        const auto preview = canvas->mapFromScene(QRectF(0, 0, 1072, 1448)).boundingRect();
        QVERIFY(preview.width() <= canvas->viewport()->width() + 1);
        QVERIFY(preview.height() <= canvas->viewport()->height() + 1);
        picker->setCurrentIndex(2);
        QTRY_COMPARE(f.window.readerWindow()->size(), QSize(1264, 1680));
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(f.state().value("position").toString(), position);
        auto *previous = f.window.findChild<QPushButton *>("desktopDevicePrevious");
        QVERIFY(previous);
        auto *next = f.window.findChild<QPushButton *>("desktopDeviceNext");
        QVERIFY(next);
        QVERIFY(previous->isVisible() && next->isVisible());
        QVERIFY(previous->isEnabled() && next->isEnabled());
        const int beforeButtons = f.state().value("page").toInt();
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(f.state().value("page").toInt() > beforeButtons, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QTest::mouseClick(previous, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(f.state().value("page").toInt(), beforeButtons, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        const auto afterButtons = f.state().value("position").toString();
        picker->setCurrentIndex(0);
        QVERIFY(!previous->isVisible() && !next->isVisible());
        QCOMPARE(opened.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QCOMPARE(f.window.readerWindow()->size(), canvas->size() * 2);
        QCOMPARE(canvas->transform().m11(), 0.5);
        QCOMPARE(readingPpi.load(), 300);
        QCOMPARE(f.state().value("position").toString(), afterButtons);
    }
    void canvasBookLifecycle_data() {
        QTest::addColumn<QString>("filename");
        int position = 0;
        const auto row = [&](const char *name, const QString &filename) {
            if (test::shardContains(position++, "KOBALT_TEST_DATA_SHARD")) {
                QTest::newRow(name) << filename;
            }
        };
        row("normal", "text.epub");
        row("long-public-domain", "public-domain-ru.epub");
        row("linked", "links.epub");
        row("arabic", "multilingual-ar.epub");
        row("japanese", "public-domain-ja.epub");
    }
    void canvasBookLifecycle() {
        QFETCH(QString, filename);
        Fixture f;
        // Hardware-mode runtime reports ordinary saves only with this opt-in.
        QFile liveState(f.directory.path() + "/live-state");
        QVERIFY(liveState.open(QIODevice::WriteOnly));
        liveState.close();
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        QSignalSpy failed(&f.window, &desktop::Window::bookFailed);
        const auto path = QStringLiteral(KOBALT_FIXTURES) + "/" + filename;
        f.window.openBook(path);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.reportedState().value("busy").toBool(), 30000);
        QCOMPARE(failed.count(), 0);
        const auto first = f.reportedState();
        QVERIFY(first.value("reading_image_height").toInt() > 0);
        QVERIFY(first.value("pages").toInt() > 1);
        QVERIFY(!first.value("position").toString().isEmpty());
        auto *reader = f.window.readerWindow();
        QVERIFY(reader);
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(page);
        const int forwardX = first.value("page_progression_rtl").toBool() ? page->width() / 6 : page->width() * 5 / 6;
        QVERIFY(f.canvasClick(page, QPoint(forwardX, page->height() / 2)));
        QTRY_VERIFY_WITH_TIMEOUT(f.reportedState().value("page").toInt() > first.value("page").toInt(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.reportedState().value("busy").toBool(), 30000);
        const auto turned = f.reportedState();
        const auto position = turned.value("position").toString();
        QVERIFY(!position.isEmpty());
        QVERIFY(position != first.value("position").toString());
        // Observe production saves without forcing a checkpoint from the test.
        QVERIFY(f.canvasClick(page, page->rect().center()));
        auto *back = reader->findChild<QPushButton *>("rrReturn");
        QVERIFY(back);
        QTRY_VERIFY(back->isVisible() && back->isEnabled());
        QVERIFY(f.canvasClick(back, back->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
        auto *title = f.window.findChild<QLabel *>("androidStartTitle");
        QVERIFY(title);
        QTRY_VERIFY(title->isVisible());
        QVERIFY(f.window.isVisible());
        f.window.openBook(path);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.reportedState().value("busy").toBool(), 30000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(f.reportedState().value("position").toString(), position);
        QCOMPARE(f.reportedState().value("page").toInt(), turned.value("page").toInt());
        QVERIFY(!f.window.grab().isNull());
    }
    void displayChangesRestoreCanvasScale() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *canvas = f.window.findChild<QGraphicsView *>();
        QVERIFY(canvas);
        auto *handle = f.window.windowHandle();
        QVERIFY(handle);
        auto *screen = handle->screen();
        QVERIFY(screen);
        auto *picker = f.window.findChild<QComboBox *>("desktopScreenPicker");
        QVERIFY(picker);
        for (const int preset : {0, 1, 2}) {
            picker->setCurrentIndex(preset);
            QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
            QTest::qWait(150);
            const auto viewport = f.window.readerWindow()->size();
            const auto transform = canvas->transform();
            if (preset == 0) {
                QCOMPARE(viewport, canvas->size() * 2);
                QCOMPARE(transform.m11(), 0.5);
            }
            for (const char *signal : {"logicalDotsPerInchChanged", "physicalDotsPerInchChanged"}) {
                canvas->setTransform(QTransform::fromScale(.125, .125));
                QVERIFY(QMetaObject::invokeMethod(screen, signal, Qt::DirectConnection,
                                                  Q_ARG(qreal, screen->logicalDotsPerInch())));
                QTRY_COMPARE(canvas->transform(), transform);
                QCOMPARE(f.window.readerWindow()->size(), viewport);
            }
            canvas->setTransform(QTransform::fromScale(.125, .125));
            QVERIFY(QMetaObject::invokeMethod(handle, "screenChanged", Qt::DirectConnection, Q_ARG(QScreen *, screen)));
            QTRY_COMPARE(canvas->transform(), transform);
            QCOMPARE(f.window.readerWindow()->size(), viewport);
        }
    }
    void resizeAndNavigate() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        const int firstPage = f.state().value("page").toInt();
        QTest::keyClick(&f.window, Qt::Key_Right);
        QTRY_VERIFY_WITH_TIMEOUT(f.state().value("page").toInt() > firstPage, 10000);
        QTest::keyClick(&f.window, Qt::Key_Left);
        QTRY_COMPARE_WITH_TIMEOUT(f.state().value("page").toInt(), firstPage, 10000);
        const int before = f.state().value("viewport_width").toInt();
        f.window.resize(920, 600);
        const int expected = f.window.centralWidget()->width() * 2;
        QVERIFY(expected != before);
        QTRY_COMPARE_WITH_TIMEOUT(f.state().value("viewport_width").toInt(), expected, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        const auto state = f.state();
        QCOMPARE(state.value("reading_image_height").toInt(), state.value("viewport_height").toInt() -
                                                                  state.value("reading_top").toInt() -
                                                                  state.value("reading_bottom").toInt());
        QVERIFY(!f.window.grab().isNull());
        f.window.close();
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
    }
    void fontHoverKeepsHeaderBorder() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        reader->findChild<ReadingPage *>("rrPage")->tapped(reader->rect().center());
        reader->findChild<QPushButton *>("rrNavText")->click();
        auto *font = reader->findChild<QPushButton *>("rrFont");
        QTRY_VERIFY(font->isVisible() && font->isEnabled());
        font->click();
        auto *close = reader->findChild<QPushButton *>("rrFontClose");
        QVERIFY(close && close->isVisible());
        auto *canvas = f.window.findChild<QGraphicsView *>();
        QVERIFY(canvas);
        QCoreApplication::processEvents();
        const auto before = canvas->viewport()->grab().toImage();
        const auto point = canvas->mapFromScene(close->mapTo(reader, close->rect().center()));
        const auto outside = canvas->mapFromScene(close->parentWidget()->mapTo(reader, QPoint(20, 20)));
        // Send through the view so the proxy invalidates its device cache.
        for (int pass = 0; pass < 3; ++pass)
            for (const bool hovering : {true, false}) {
                const auto position = hovering ? point : outside;
                QMouseEvent move(QEvent::MouseMove, QPointF(position),
                                 QPointF(canvas->viewport()->mapToGlobal(position)), Qt::NoButton, Qt::NoButton,
                                 Qt::NoModifier);
                QApplication::sendEvent(canvas->viewport(), &move);
                QCoreApplication::processEvents();
                QCOMPARE(close->underMouse(), hovering);
                QCOMPARE(canvas->viewport()->grab().toImage(), before);
            }
    }
    void variableFontWeightPreviewPersists() {
        const auto face = test::variableFont(test::variableFontInputs(engineFontInputs(*assets, {}, {}, {})));
        if (!face.weightAxis.available())
            QSKIP("Set KOBALT_TEST_VARIABLE_FONTS to exercise a variable font");
        const auto axis = face.weightAxis;
        const int initialWeight = qBound(axis.minimum, defaultVariableFontWeight, axis.maximum);
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(page);
        QVERIFY(f.canvasClick(page, page->rect().center()));
        auto *text = reader->findChild<QPushButton *>("rrNavText");
        QVERIFY(text);
        QVERIFY(f.canvasClick(text, text->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(f.state()["preview_ready"].toBool() && !f.state()["preview_busy"].toBool(), 30000);
        auto *font = reader->findChild<QPushButton *>("rrFont");
        QVERIFY(font);
        QVERIFY(f.canvasClick(font, font->rect().center()));
        QPushButton *choice = nullptr;
        for (int i = 0; i < 12 && !choice; ++i) {
            for (auto *label : reader->findChildren<QLabel *>())
                if (label->isVisible() && label->objectName().endsWith("Title") &&
                    label->text() == fontDisplayName(face.family))
                    choice = qobject_cast<QPushButton *>(label->parentWidget());
            if (choice)
                break;
            auto *next = reader->findChild<QPushButton *>("rrFontPagerNext");
            QVERIFY(next);
            QVERIFY(f.canvasClick(next, next->rect().center()));
        }
        QVERIFY(choice);
        QVERIFY(f.canvasClick(choice, choice->rect().center()));
        QTRY_COMPARE_WITH_TIMEOUT(f.state()["preview_settings"].toObject()["font_face"].toString(), face.family, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["preview_busy"].toBool(), 30000);
        auto *close = reader->findChild<QPushButton *>("rrFontClose");
        QVERIFY(close);
        QVERIFY(f.canvasClick(close, close->rect().center()));
        auto *weight = reader->findChild<QSlider *>("rrVariableFontWeight");
        QVERIFY(weight);
        QTRY_VERIFY(weight->isVisible() && weight->isEnabled());
        auto *optical = reader->findChild<QSlider *>("rrOpticalSize");
        QVERIFY(optical);
        QCOMPARE(optical->parentWidget()->isVisible(), face.opticalSizeAxis.available());
        auto *thickness = reader->findChild<QSlider *>("rrFontWeight");
        QVERIFY(thickness);
        QVERIFY(thickness->isVisible() && thickness->isEnabled());
        QCOMPARE(thickness->minimum(), 0);
        QCOMPARE(thickness->maximum(), 12);
        QVERIFY(f.canvasClick(thickness, QPoint(thickness->width() / 2, thickness->height() / 2)));
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["preview_busy"].toBool(), 30000);
        const int selectedThickness = f.state()["preview_settings"].toObject()["weight_steps"].toInt();
        QVERIFY(selectedThickness > 0);
        QCOMPARE(f.state()["preview_settings"].toObject()["font_weight"].toInt(), initialWeight);
        QCOMPARE(weight->minimum(), axis.minimum);
        QCOMPARE(weight->maximum(), axis.maximum);
        QCOMPARE(weight->singleStep(), 10);
        QCOMPARE(weight->value(), initialWeight);
        QVERIFY(f.canvasClick(weight,
                              QPoint(weight->width() * (initialWeight < (axis.minimum + axis.maximum) / 2 ? 3 : 1) / 4,
                                     weight->height() / 2)));
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["preview_busy"].toBool(), 30000);
        const auto draft = f.state()["preview_settings"].toObject();
        const int selected = draft["font_weight"].toInt();
        QVERIFY(selected != initialWeight && selected >= axis.minimum && selected <= axis.maximum);
        QVERIFY((selected - axis.minimum) % 10 == 0 || selected == axis.maximum);
        QCOMPARE(draft["weight_steps"].toInt(), selectedThickness);
        QCOMPARE(reader->findChild<QLabel *>("rrVariableFontWeightValue")->text(), QString::number(selected));
        QVERIFY(f.canvasClick(text, text->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool() && !f.state()["preview_ready"].toBool(), 30000);
        QCOMPARE(f.state()["font_weight"].toInt(), selected);
        QCOMPARE(f.state()["weight_steps"].toInt(), selectedThickness);
        auto *back = reader->findChild<QPushButton *>("rrReturn");
        QVERIFY(back);
        if (!back->isVisible())
            QVERIFY(f.canvasClick(page, page->rect().center()));
        QTRY_VERIFY(back->isVisible() && back->isEnabled());
        QVERIFY(f.canvasClick(back, back->rect().center()));
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state()["busy"].toBool(), 30000);
        QCOMPARE(f.state()["font_face"].toString(), face.family);
        QCOMPARE(f.state()["font_weight"].toInt(), selected);
        QCOMPARE(f.state()["weight_steps"].toInt(), selectedThickness);
    }
    void nativeUiPreferencesPersist() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        auto *page = reader->findChild<ReadingPage *>("rrPage");
        QVERIFY(page);
        QVERIFY(!page->slotContents[0].empty());
        QVERIFY(page->showHeader());
        page->tapped(page->rect().center());
        auto *toolbar = reader->findChild<QWidget *>("rrControlsTop");
        QVERIFY(toolbar);
        const int compactHeight = toolbar->height();
        QVERIFY(reader->findChild<QLabel *>("rrMenuClock")->isHidden());
        auto *settings = reader->findChild<QPushButton *>("rrSettings");
        QVERIFY(settings);
        settings->click();
        auto *nativeTab = reader->findChild<QPushButton *>("rrSettingsUI");
        QVERIFY(nativeTab);
        nativeTab->click();
        auto *hideStatus = reader->findChild<QCheckBox *>("rrHideStatusBar");
        auto *hideDevice = reader->findChild<QCheckBox *>("rrHideDeviceOptions");
        QVERIFY(hideStatus && hideDevice);
        QVERIFY(hideStatus->isChecked());
        QVERIFY(hideDevice->isChecked());
        QVERIFY(hideStatus->isVisible());
        auto *animation = reader->findChild<QCheckBox *>("rrPageAnimations");
        auto *refresh = reader->findChild<QPushButton *>("rrScreenRefresh");
        QVERIFY(animation && refresh);
        QVERIFY(animation->isHidden());
        QVERIFY(refresh->isHidden());
        hideDevice->click();
        hideStatus->click();
        reader->findChild<QPushButton *>("rrSettingsDisplay")->click();
        QVERIFY(animation->isVisible());
        QVERIFY(refresh->isVisible());
        QVERIFY(!animation->isEnabled());
        page->tapped(page->rect().center());
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        QTRY_VERIFY(!reader->findChild<QWidget *>("rrGeneralSettings")->isVisible());
        QVERIFY(!page->slotContents[0].empty());
        page->tapped(page->rect().center());
        QTRY_VERIFY(toolbar->height() > compactHeight);
        QVERIFY(reader->findChild<QLabel *>("rrMenuClock")->isVisible());
        reader->findChild<QPushButton *>("rrNavChapters")->click();
        auto *chapters = reader->findChild<QWidget *>("rrChoices");
        QVERIFY(chapters);
        QTRY_VERIFY(chapters->isVisible());
        QCOMPARE(chapters->y(), int(ui::metrics::chrome::statusHeight));
        const auto prefs = storage::readJson(f.paths.preferences());
        QVERIFY(prefs.contains("native_hide_status_bar"));
        QVERIFY(!prefs.value("native_hide_status_bar").toBool());
        QVERIFY(!prefs.value("native_hide_device_options").toBool());
        reader->findChild<QPushButton *>("rrReturn")->click();
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
        auto *reopened = f.window.readerWindow()->findChild<ReadingPage *>("rrPage");
        QVERIFY(reopened);
        QVERIFY(!reopened->slotContents[0].empty());
    }
    void hiddenDeviceOptionsKeepCompactRows() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        reader->findChild<ReadingPage *>("rrPage")->tapped(reader->rect().center());
        reader->findChild<QPushButton *>("rrSettings")->click();
        for (bool hidden : {true, false, true}) {
            reader->findChild<QPushButton *>("rrSettingsUI")->click();
            auto *toggle = reader->findChild<QCheckBox *>("rrHideDeviceOptions");
            if (toggle->isChecked() != hidden)
                toggle->click();
            for (const auto &tab : {"Advanced", "Display"}) {
                reader->findChild<QPushButton *>(QString("rrSettings") + tab)->click();
                const QStringList names = QString(tab) == "Advanced"
                                              ? QStringList{"rrInheritTextLayout", "rrInheritHeaderFooter"}
                                              : QStringList{"rrDarkMode", "rrBionicReading"};
                for (const auto &name : names) {
                    auto *option = reader->findChild<QCheckBox *>(name);
                    QVERIFY(option);
                    QTRY_COMPARE(option->height(), option->heightForWidth(option->width()));
                }
                if (QString(tab) == "Advanced") {
                    auto *lastDefault = reader->findChild<QCheckBox *>("rrInheritHeaderFooter");
                    auto *content = reader->findChild<QWidget *>("rrAdvancedContentSection");
                    QVERIFY(lastDefault && content);
                    auto *reset = reader->findChild<QPushButton *>("rrRestoreDefaults");
                    QTRY_COMPARE(reset->y(), lastDefault->geometry().bottom() + 1);
                    QTRY_COMPARE(content->y(), reset->geometry().bottom() + 1);
                }
            }
        }
    }
    void settingsRowsDrawBottomBorders() {
        Fixture f;
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.state().value("busy").toBool(), 30000);
        auto *reader = f.window.readerWindow();
        reader->findChild<ReadingPage *>("rrPage")->tapped(reader->rect().center());
        reader->findChild<QPushButton *>("rrSettings")->click();
        for (const auto &tab : {"Display", "Advanced", "UI", "Stats"}) {
            reader->findChild<QPushButton *>(QString("rrSettings") + tab)->click();
            auto *pages = reader->findChild<QStackedWidget *>("rrSettingsPages");
            for (auto *row : pages->currentWidget()->findChildren<QWidget *>()) {
                auto *action = dynamic_cast<ui::CaptionButton *>(row);
                if (!row->isVisible() ||
                    (!qobject_cast<QCheckBox *>(row) &&
                     !(action && action->presentation() == ui::CaptionButton::Presentation::ListAction)))
                    continue;
                const auto original = row->palette();
                for (bool dark : {false, true}) {
                    auto palette = original;
                    palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
                    palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
                    row->setPalette(palette);
                    const auto grab = row->grab();
                    const auto image = grab.toImage();
                    const int pixelRatio = qRound(grab.devicePixelRatioF());
                    const int x = image.width() / 2;
                    const int borderPixels = int(ui::metrics::control::border) * pixelRatio;
                    for (int offset = 1; offset <= borderPixels; ++offset) {
                        QCOMPARE(image.pixelColor(x, image.height() - offset), ui::dividerColor(row));
                    }
                    QCOMPARE(image.pixelColor(x, image.height() - borderPixels - 1), palette.color(QPalette::Window));
                }
                row->setPalette(original);
            }
        }
    }
    void failedBookAndCloseReturnToStart() {
        Fixture f;
        QVERIFY(f.window.windowTitle().isEmpty());
        QVERIFY(f.window.windowHandle()->title().isEmpty());
        QSignalSpy failed(&f.window, &desktop::Window::bookFailed);
        f.window.openBook(f.directory.path() + "/missing.epub");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 10000);
        auto *title = f.window.findChild<QLabel *>("androidStartTitle");
        QVERIFY(title);
        QVERIFY(title->isVisible());
        const auto errorTitle = title->text();
        QSignalSpy opened(&f.window, &desktop::Window::bookOpened);
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        auto *back = f.window.readerWindow()->findChild<QPushButton *>("rrReturn");
        QVERIFY(f.window.windowTitle().isEmpty());
        QVERIFY(f.window.windowHandle()->title().isEmpty());
        QVERIFY(back);
        back->click();
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
        QTRY_VERIFY(title->isVisible());
        QVERIFY(f.window.windowTitle().isEmpty());
        QVERIFY(title->text() != errorTitle);
        QVERIFY(f.window.isVisible());
        f.window.openBook(f.book());
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
    }
    void closeDuringOpen() {
        Fixture f;
        f.window.openBook(f.book());
        f.window.close();
        QTRY_VERIFY_WITH_TIMEOUT(f.window.idle(), 30000);
        QTRY_VERIFY(!f.window.isVisible());
    }
};
int main(int argc, char **argv) {
    FontCopies fontCopies;
    copies = &fontCopies;
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    AssetBundle bundle(QStringLiteral(KOBALT_TEST_ASSET_ROOT), QStringLiteral(KOBALT_VERSION));
    assets = &bundle;
    if (!bundle.available() || !ui::initializeInterfaceFonts(bundle))
        return 2;
    for (const auto &file : bundledFontFiles(bundle))
        if (QFontDatabase::addApplicationFont(file) < 0)
            return 2;
    readingPpi.store(150);
    ui::setDensity(150);
    // Worker shutdown is connected to aboutToQuit, as in the application.
    DesktopTest test;
    QTimer::singleShot(0, &app, [&] {
        app.exit(QTest::qExec(&test, test::shardArguments(test, argc, argv)));
    });
    return app.exec();
}
#include "window_test.moc"
