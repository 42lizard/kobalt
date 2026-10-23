#include "interfaces/reader/ui/controls/sliders.h"
#include "platform/android/start_screen.h"
#include "platform/android/canvas.h"
#include "platform/android/light.h"
#include "platform/android/refresh.h"
#include "platform/android/boox_refresh.h"
#include "platform/android/boox_frame_queue.h"
#include "interfaces/reader/panels/light_panel.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/ui/style/theme.h"
#include <QDir>
#include "interfaces/reader/views/opening.h"
#include "interfaces/reader/panels/search_panel.h"
#include "host_services.h"
#include <QScrollArea>
#include <QBuffer>
#include <QProgressBar>
#include "assets/assets.h"
#include "interfaces/reader/ui/style/fonts.h"
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPaintEvent>
#include <QGraphicsProxyWidget>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedLayout>
#include <QResizeEvent>
#include <QTest>

using namespace kobalt;
class PaintRegions final : public QObject {
public:
    QRegion painted;
    int count = 0;
    void clear() {
        painted = {};
        count = 0;
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) {
            painted += static_cast<QPaintEvent *>(event)->region();
            ++count;
        }
        return false;
    }
};
class EdgeControls final : public QWidget {
public:
    QPushButton *bottom = new QPushButton("Bottom navigation", this);
    QPushButton *top = new QPushButton("Status", this);

protected:
    void resizeEvent(QResizeEvent *) override {
        const int row = qRound(40 * devicePixelRatioF());
        top->setGeometry(0, 0, width(), row);
        bottom->setGeometry(0, height() - row, width(), row);
    }
};
class TestPresentation final : public WindowPresentation {
public:
    void pauseBackground(bool) override {}
    void resumeBackground() override {}
    void resumeAndRepaintBackground() override {}
    void acceptHandoff(QDialog &, const std::function<void()> &accept) override {
        accept();
    }
};
PresentationFactory presentation() {
    return [](QWidget *) {
        return std::make_unique<TestPresentation>();
    };
}
class DeleteOnTouchCancel final : public QPushButton {
public:
    using QPushButton::QPushButton;

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::TouchCancel) {
            delete this;
            return true;
        }
        return QPushButton::event(event);
    }
};
class AndroidUiTest : public QObject {
    Q_OBJECT
private slots:
    void booxRegionsUseSeparateBuffers() {
        android::BooxFrameQueue queue;
        android::RefreshFrame frame;
        const QRegion header(QRect(500, 10, 50, 20)), footer(QRect(250, 760, 60, 20));
        const QRegion body(QRect(20, 60, 560, 680));
        frame.screenPixels = header | body | footer;
        frame.textPixels = body;
        QList<android::RefreshFrame> buffers;
        const auto submit = [&](const android::RefreshFrame &value) {
            buffers.append(value);
            return true;
        };
        QVERIFY(queue.prepare(frame, submit));
        QCOMPARE(buffers.size(), 1);
        QCOMPARE(buffers.first().textPixels, body);
        QCOMPARE(buffers.first().screenPixels, body);
        QVERIFY(queue.pending());
        while (queue.pending()) {
            QVERIFY(queue.present(submit));
            QVERIFY(buffers.size() <= 3);
        }
        QCOMPARE(buffers.size(), 3);
        QRegion chrome;
        for (int i = 1; i < buffers.size(); ++i) {
            QVERIFY(buffers[i].textPixels.isEmpty());
            QCOMPARE(buffers[i].screenPixels.rectCount(), 1);
            chrome += buffers[i].screenPixels;
        }
        QCOMPARE(chrome, header | footer);
        // A cleanup supersedes queued partial updates and is submitted once.
        QVERIFY(queue.prepare(frame, submit));
        android::RefreshFrame cleanup;
        cleanup.clean = true;
        cleanup.screenPixels = body;
        QVERIFY(queue.prepare(cleanup, submit));
        QVERIFY(buffers.last().clean);
        QVERIFY(!queue.pending());
    }
    void booxPendingRegionsKeepLatestPolicy() {
        android::BooxFrameQueue queue;
        android::RefreshFrame frame;
        frame.screenPixels = QRegion(QRect(10, 20, 100, 200));
        frame.textPixels = frame.screenPixels;
        QVERIFY(!queue.prepare(frame, [](const android::RefreshFrame &) {
            return false;
        }));
        QVERIFY(queue.pending());
        frame.textPixels = {};
        android::RefreshFrame accepted;
        QVERIFY(queue.prepare(frame, [&](const android::RefreshFrame &value) {
            accepted = value;
            return true;
        }));
        QCOMPARE(accepted.screenPixels, frame.screenPixels);
        QVERIFY(accepted.textPixels.isEmpty());
        QVERIFY(!queue.pending());
        QVERIFY(!queue.prepare(frame, [](const android::RefreshFrame &) {
            return false;
        }));
        queue.reset();
        QVERIFY(!queue.pending());
    }
    void booxKeepsDisjointPaintDamage() {
        QWidget reader, viewport;
        const QRegion damage = QRegion(QRect(10, 20, 20, 30)) | QRegion(QRect(200, 300, 20, 30));
        const auto frame = android::takeRefreshFrame(&reader, &viewport, damage);
        QCOMPARE(frame.screenPixels.rectCount(), 2);
        QVERIFY(!frame.screenPixels.contains(frame.screenPixels.boundingRect().center()));
    }
    void booxPageTurnRegions_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("accept");
        QTest::newRow("light") << false << true;
        QTest::newRow("dark") << true << true;
        QTest::newRow("retry") << false << false;
    }
    void booxPageTurnRegions() {
        QFETCH(bool, dark);
        QFETCH(bool, accept);
        QWidget reader, viewport;
        reader.resize(600, 800);
        ReadingPage page(&reader, [] {});
        page.setFont(ui::headerFooterFont());
        page.resize(reader.size());
        page.chrome.progressBar = false;
        page.chrome.slotFields = {headerfooter::Field::Time,     headerfooter::Field::BookTitle,
                                  headerfooter::Field::BookPage, headerfooter::Field::Author,
                                  headerfooter::Field::Chapter,  headerfooter::Field::BatteryPercent};
        page.slotContents[0].text = "09:00";
        page.slotContents[1].text = "Unchanged title";
        page.slotContents[2].text = "35 / 4000";
        page.slotContents[3].text = "Unchanged author";
        page.slotContents[4].text = "Unchanged chapter";
        page.slotContents[5].text = "100%";
        auto palette = page.palette();
        palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        page.setPalette(palette);
        page.image = QImage(600, 600, QImage::Format_RGB32);
        page.image.fill(Qt::white);
        page.refreshContent();
        reader.show();
        QCoreApplication::processEvents();
        android::BooxRefreshPolicy policy;
        const QRegion paint(page.rect());
        QVERIFY(policy.prepare(&reader, &viewport, &page, paint, [](const android::RefreshFrame &) {
            return true;
        }));
        const int top = page.topHeight(), bottom = page.bottomHeight();
        QVERIFY(top > 0 && bottom > 0);
        page.slotContents[0].text = "09:01";
        page.slotContents[2].text = "36 / 4000";
        page.image.fill(Qt::gray);
        page.refreshContent(true);
        reader.setProperty("boox_text_frame", !dark);
        android::RefreshFrame frame;
        const auto submit = [&](const android::RefreshFrame &value) {
            frame = value;
            return accept;
        };
        QCOMPARE(policy.prepare(&reader, &viewport, &page, paint, submit), accept);
        if (!accept) {
            QVERIFY(reader.property("boox_text_frame").toBool());
            const auto rejected = frame.screenPixels;
            QVERIFY(policy.prepare(&reader, &viewport, &page, paint, [&](const android::RefreshFrame &value) {
                frame = value;
                return true;
            }));
            QCOMPARE(frame.screenPixels, rejected);
        }
        const QPoint origin = viewport.mapToGlobal(QPoint()) * viewport.devicePixelRatioF();
        const auto pixels = frame.screenPixels.translated(-origin);
        QVERIFY(pixels.intersects(QRect(0, 0, 200, top)));
        QVERIFY(pixels.intersects(QRect(400, 0, 200, top)));
        // Stable monospace positions leave the slash and total untouched.
        QVERIFY((pixels & QRegion(QRect(400, 0, 200, top))).boundingRect().width() < 60);
        QVERIFY(!pixels.intersects(QRect(200, 0, 200, top)));
        QVERIFY(!pixels.intersects(QRect(0, 800 - bottom, 600, bottom)));
        const QRegion body(QRect(0, top, 600, 800 - top - bottom));
        QCOMPARE(frame.textPixels.translated(-origin), dark ? QRegion() : body);
        // Visible reading controls keep their conservative paint damage.
        QWidget controls(&reader);
        controls.setObjectName("rrControlsTop");
        controls.show();
        page.image.fill(Qt::yellow);
        page.refreshContent(true);
        QVERIFY(policy.prepare(&reader, &viewport, &page, paint, [&](const android::RefreshFrame &value) {
            frame = value;
            return true;
        }));
        QCOMPARE(frame.screenPixels.translated(-origin), paint);
        controls.hide();
        // Resizing invalidates the strip comparison even without an explicit reset.
        page.resize(590, 800);
        page.image.fill(Qt::green);
        page.refreshContent(true);
        QVERIFY(policy.prepare(&reader, &viewport, &page, paint, [&](const android::RefreshFrame &value) {
            frame = value;
            return true;
        }));
        QCOMPARE(frame.screenPixels.translated(-origin), paint);
        // Reader close and viewport changes can explicitly release the cache.
        policy.reset();
        QVERIFY(policy.prepare(&reader, &viewport, &page, paint, [&](const android::RefreshFrame &value) {
            frame = value;
            return true;
        }));
        QCOMPARE(frame.screenPixels.translated(-origin), paint);
    }
    void booxMissingSurfaceRetainsPendingFrame() {
        QWidget reader, viewport;
        reader.setProperty("reader_full_flash", true);
        reader.setProperty("boox_text_frame", true);
        const QRegion damage(QRect(10, 20, 100, 200));
        QVERIFY(!android::prepareRefreshFrame(&reader, &viewport, damage, [](const android::RefreshFrame &) {
            return false;
        }));
        QVERIFY(reader.property("reader_full_flash").toBool());
        QVERIFY(reader.property("boox_text_frame").toBool());
        bool clean = false, text = false;
        QVERIFY(android::prepareRefreshFrame(&reader, &viewport, damage, [&](const android::RefreshFrame &frame) {
            clean = frame.clean;
            text = frame.text;
            return true;
        }));
        QVERIFY(clean);
        QVERIFY(text);
        QVERIFY(!reader.property("reader_full_flash").toBool());
        QVERIFY(!reader.property("boox_text_frame").toBool());
    }
    void booxCleanupUsesSharedReaderPolicy() {
        QWidget reader, viewport;
        reader.setProperty("reader_full_flash", true);
        reader.setProperty("boox_text_frame", true);
        const auto frame = android::takeRefreshFrame(&reader, &viewport, QRegion(QRect(10, 20, 100, 200)));
        QVERIFY(frame.clean);
        QVERIFY(frame.text);
        QVERIFY(!reader.property("reader_full_flash").toBool());
        QVERIFY(!reader.property("boox_text_frame").toBool());
        const auto next = android::takeRefreshFrame(&reader, &viewport, QRegion(QRect(10, 20, 100, 200)));
        QVERIFY(!next.clean);
        QVERIFY(!next.text);
    }
    void booxLightPanel_data() {
        QTest::addColumn<int>("ppi");
        QTest::addColumn<bool>("dark");
        QTest::newRow("light-212") << 212 << false;
        QTest::newRow("dark-212") << 212 << true;
        QTest::newRow("light-300") << 300 << false;
        QTest::newRow("dark-300") << 300 << true;
    }
    void booxLightPanel() {
        QFETCH(int, ppi);
        QFETCH(bool, dark);
        struct DensityRestore {
            int original = ui::density();
            ~DensityRestore() {
                ui::setDensity(original);
            }
        } restore;
        ui::setDensity(ppi);
        android::Light light([](int operation, int type, int) {
            return operation == 1 ? 32 : type == 7 ? 28 : 12;
        });
        QWidget parent;
        parent.resize(ui::px(1264), ui::px(1680));
        QPalette palette = parent.palette();
        palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Base, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::Button, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::ButtonText, dark ? Qt::white : Qt::black);
        parent.setPalette(palette);
        parent.setProperty("reader_dark", dark);
        LightPanel panel(light, &parent);
        for (auto *widget : parent.findChildren<QWidget *>()) {
            widget->setPalette(palette);
            if (widget->property("reader_style").isValid())
                ui::readerStyle(widget, widget->property("reader_style").toString());
        }
        parent.show();
        QVERIFY(panel.open());
        QCoreApplication::processEvents();
        QVERIFY(panel.findChild<QWidget *>("rrAutomaticWarmth")->isHidden());
        QVERIFY(parent.rect().contains(panel.geometry()));
        QCOMPARE(panel.findChild<QWidget *>("rrBrightnessValue")->palette().color(QPalette::WindowText),
                 QColor(dark ? Qt::white : Qt::black));
        const auto directory = qEnvironmentVariable("KOBALT_BOOX_CAPTURES");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(panel.grab().save(directory + '/' + QTest::currentDataTag() + ".png"));
        }
    }
    void booxLightLevelsAndCancellation() {
        int brightness = 28, warmth = 0;
        bool failed = false;
        android::Light light([&](int operation, int type, int value) {
            if (failed)
                return -1;
            int &level = type == 7 ? brightness : warmth;
            if (operation == 1)
                return 32;
            if (operation == 2)
                level = value;
            return level;
        });
        QVERIFY(light.initialize());
        QVERIFY(light.hasWarmth());
        QVERIFY(!light.hasAutomaticWarmth());
        QWidget parent;
        parent.resize(600, 800);
        LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        QVERIFY(panel.findChild<QWidget *>("rrAutomaticWarmth")->isHidden());
        QVERIFY(!panel.findChild<QWidget *>("rrWarmth")->isHidden());
        panel.hide();
        QCOMPARE(light.brightness(), 88);
        QVERIFY(light.beginGesture());
        QCOMPARE(light.setGestureLevel(50), 50);
        QCOMPARE(brightness, 16);
        QVERIFY(light.beginGesture());
        light.finishGesture(false);
        QCOMPARE(brightness, 28);
        QVERIFY(light.beginGesture());
        QCOMPARE(light.setGestureLevel(100), 100);
        light.finishGesture(true);
        QCOMPARE(brightness, 32);
        QCOMPARE(light.setWarmth(12), 12);
        QCOMPARE(warmth, 12);
        QCOMPARE(brightness, 32);
        QVERIFY(light.saveWarmth());
        failed = true;
        QVERIFY(!light.initialize());
        QVERIFY(!light.beginGesture());
        QCOMPARE(light.brightness(), -1);
        QCOMPARE(light.setGestureLevel(50), -1);
    }

    void cancelledCanvasTouchRestoresSlider_data() {
        QTest::addColumn<bool>("secondFinger");
        QTest::newRow("touch-cancel") << false;
        QTest::newRow("second-finger") << true;
    }
    void cancelledCanvasTouchRestoresSlider() {
        QFETCH(bool, secondFinger);
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(400, 300);
        auto *host = new QWidget;
        auto *slider = new ui::SettingSlider(host, 0, 100);
        slider->setGeometry(0, 0, 350, 100);
        slider->setValue(25);
        int commits = 0;
        slider->committed = [&](int) {
            ++commits;
        };
        canvas.setContent(host);
        canvas.fitContent();
        canvas.show();
        host->show();
        auto *device = QTest::createTouchDevice();
        QTouchEvent begin(QEvent::TouchBegin, device, Qt::NoModifier,
                          {QEventPoint(0, QEventPoint::Pressed, QPointF(), QPointF())});
        QApplication::sendEvent(canvas.viewport(), &begin);
        QCOMPARE(slider->value(), 0);
        QTouchEvent cancelled(
            secondFinger ? QEvent::TouchUpdate : QEvent::TouchCancel, device, Qt::NoModifier,
            secondFinger ? QList<QEventPoint>{QEventPoint(0, QEventPoint::Stationary, QPointF(), QPointF()),
                                              QEventPoint(1, QEventPoint::Pressed, QPointF(10, 10), QPointF(10, 10))}
                         : QList<QEventPoint>{});
        QApplication::sendEvent(canvas.viewport(), &cancelled);
        QCOMPARE(slider->value(), 25);
        QCOMPARE(commits, 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(300, 50), QPointF(300, 50), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(slider, &move);
        QCOMPARE(slider->value(), 25);
    }
    void canvasCancellationCanDeleteTarget() {
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(400, 300);
        auto *host = new QWidget;
        QPointer<DeleteOnTouchCancel> button = new DeleteOnTouchCancel(host);
        button->setGeometry(0, 0, 350, 100);
        canvas.setContent(host);
        canvas.fitContent();
        canvas.show();
        host->show();
        auto *device = QTest::createTouchDevice();
        QTouchEvent begin(QEvent::TouchBegin, device, Qt::NoModifier,
                          {QEventPoint(0, QEventPoint::Pressed, QPointF(), QPointF())});
        QApplication::sendEvent(canvas.viewport(), &begin);
        QVERIFY(button->isDown());
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QApplication::sendEvent(canvas.viewport(), &cancel);
        QVERIFY(button.isNull());
        QApplication::sendEvent(canvas.viewport(), &cancel);
    }
    void canvasCancellationKeepsReentrantGesture() {
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(400, 300);
        auto *host = new QWidget;
        auto *first = new QPushButton(host);
        first->setGeometry(0, 0, 180, 100);
        auto *second = new QPushButton(host);
        second->setGeometry(200, 0, 180, 100);
        QSignalSpy firstClicks(first, &QPushButton::clicked), secondClicks(second, &QPushButton::clicked);
        canvas.setContent(host);
        canvas.fitContent();
        canvas.show();
        host->show();
        auto *device = QTest::createTouchDevice();
        const auto send = [&](QEvent::Type type, QPointF position) {
            QTouchEvent event(type, device, Qt::NoModifier,
                              {QEventPoint(0, type == QEvent::TouchBegin ? QEventPoint::Pressed : QEventPoint::Released,
                                           position, position)});
            QApplication::sendEvent(canvas.viewport(), &event);
        };
        send(QEvent::TouchBegin, QPointF(50, 50));
        QVERIFY(first->isDown());
        // Cancellation callbacks may begin the next gesture before cancel returns.
        bool firstWasReleased = false;
        canvas.cancelGesture = [&] {
            firstWasReleased = !first->isDown();
            first->move(200, 0);
            second->move(0, 0);
            send(QEvent::TouchBegin, QPointF());
        };
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QApplication::sendEvent(canvas.viewport(), &cancel);
        QVERIFY(firstWasReleased);
        QVERIFY(second->isDown());
        send(QEvent::TouchEnd, QPointF());
        QCOMPARE(firstClicks.count(), 0);
        QCOMPARE(secondClicks.count(), 1);
        QVERIFY(!second->isDown());
    }
    void canvasReleaseCanDeleteWidgets_data() {
        QTest::addColumn<bool>("deleteContent");
        QTest::newRow("target") << false;
        QTest::newRow("content") << true;
    }
    void canvasReleaseCanDeleteWidgets() {
        QFETCH(bool, deleteContent);
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(400, 300);
        QPointer<QWidget> host = new QWidget;
        QPointer<QPushButton> button = new QPushButton(host);
        button->setGeometry(0, 0, 350, 100);
        int clicks = 0;
        connect(button, &QPushButton::clicked, &canvas, [&] {
            ++clicks;
            if (deleteContent)
                delete host.data();
            else
                delete button.data();
        });
        canvas.setContent(host);
        canvas.fitContent();
        canvas.show();
        host->show();
        auto *device = QTest::createTouchDevice();
        QTouchEvent begin(QEvent::TouchBegin, device, Qt::NoModifier,
                          {QEventPoint(0, QEventPoint::Pressed, QPointF(50, 50), QPointF(50, 50))});
        QApplication::sendEvent(canvas.viewport(), &begin);
        QVERIFY(button->isDown());
        QTouchEvent end(QEvent::TouchEnd, device, Qt::NoModifier,
                        {QEventPoint(0, QEventPoint::Released, QPointF(50, 50), QPointF(50, 50))});
        QApplication::sendEvent(canvas.viewport(), &end);
        QCOMPARE(clicks, 1);
        QVERIFY(button.isNull());
        QCOMPARE(canvas.content.isNull(), deleteContent);
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QApplication::sendEvent(canvas.viewport(), &cancel);
        QApplication::sendEvent(canvas.viewport(), &end);
        QCOMPARE(clicks, 1);
    }
    void viewportInsetsKeepEdgeControlsVisible() {
        QWidget window;
        window.resize(400, 800);
        auto *layout = new QStackedLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *loading = new QWidget;
        layout->addWidget(loading);
        auto *canvas = new android::WidgetCanvas(&window);
        layout->addWidget(canvas);
        canvas->viewportResized = [canvas] {
            canvas->fitContent();
        };
        auto *reader = new EdgeControls;
        canvas->setContent(reader);
        layout->setCurrentWidget(canvas);
        window.show();
        QSignalSpy clicks(reader->bottom, &QPushButton::clicked);
        for (const auto margins : {QMargins(), QMargins(0, 44, 0, 24), QMargins(44, 0, 24, 0)}) {
            window.setContentsMargins(margins);
            layout->activate();
            QCoreApplication::processEvents();
            QTRY_COMPARE(reader->size(), canvas->pixelSize());
            QCOMPARE(canvas->size(), window.contentsRect().size());
            const auto sceneBounds = canvas->mapFromScene(QRectF(reader->rect())).boundingRect();
            QVERIFY(sceneBounds.left() >= 0 && sceneBounds.top() >= 0);
            QVERIFY(sceneBounds.right() <= canvas->viewport()->width() &&
                    sceneBounds.bottom() <= canvas->viewport()->height());
            for (auto *control : {reader->top, reader->bottom}) {
                const auto point = canvas->mapFromScene(control->geometry().center());
                QVERIFY(canvas->viewport()->rect().contains(point));
            }
            const auto point = canvas->mapFromScene(reader->bottom->geometry().center());
            const auto before = clicks.count();
            QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QCOMPARE(clicks.count(), before + 1);
        }
        layout->setCurrentWidget(loading);
        window.resize(800, 400);
        QCoreApplication::processEvents();
        layout->setCurrentWidget(canvas);
        QCoreApplication::processEvents();
        QTRY_COMPARE(reader->size(), canvas->pixelSize());
        QCOMPARE(canvas->size(), window.contentsRect().size());
    }
    void initTestCase() {
        QApplication::setStyle("Fusion");
        AssetBundle assets(QStringLiteral(KOBALT_TEST_ASSET_ROOT), QStringLiteral(KOBALT_VERSION));
        QVERIFY(ui::initializeInterfaceFonts(assets));
    }
    void searchEditorKeepsCaseAndReleasesFocusOnReturn() {
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(1264, 1680);
        auto *owner = new QWidget;
        owner->resize(1264, 1680);
        auto *panel = new SearchPanel(owner, test::hostReaderServices().keyboard);
        panel->setGeometry(owner->rect());
        canvas.setContent(owner);
        canvas.fitContent();
        canvas.show();
        panel->open(false, 0);
        auto *query = panel->findChild<QLineEdit *>("rrSearchQuery");
        QVERIFY(query);
        QTRY_VERIFY(query->hasFocus());
        const QVariant hints = QGuiApplication::inputMethod()->queryFocusObject(Qt::ImHints, {});
        QVERIFY2(hints.isValid(), "The focused editor must expose input hints to the Android keyboard");

        class FocusEvents final : public QObject {
        public:
            int focusOut = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::FocusOut) {
                    ++focusOut;
                }
                return false;
            }
        } focusEvents;
        query->installEventFilter(&focusEvents);
        QStringList requests;
        panel->requested = [&](const QString &text) {
            requests.append(text);
        };
        QTest::keyClicks(query, "chapter");
        QTest::keyClick(query, Qt::Key_Return);
        QCOMPARE(requests, QStringList({"chapter"}));
        QVERIFY(!query->hasFocus());
        QCOMPARE(focusEvents.focusOut, 1);

        const QPoint editor = canvas.mapFromScene(query->mapTo(owner, query->rect().center()));
        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, editor);
        QTRY_VERIFY(query->hasFocus());
        QTest::keyClick(query, Qt::Key_Return);
        QCOMPARE(requests, QStringList({"chapter", "chapter"}));
        QVERIFY(!query->hasFocus());
        QCOMPARE(focusEvents.focusOut, 2);
        QVERIFY2(hints.toInt() & Qt::ImhNoAutoUppercase, "The Android keyboard must preserve lowercase search text");

        auto *otherEditor = new QLineEdit(owner);
        otherEditor->setGeometry(118, 240, 1028, 109);
        otherEditor->show();
        otherEditor->setFocus();
        QTRY_VERIFY(otherEditor->hasFocus());
        panel->stop();
        QVERIFY(otherEditor->hasFocus());
    }
    void searchInputGeometryTracksCanvasTransform() {
        QWidget window;
        window.resize(674, 900);
        android::WidgetCanvas canvas(&window);
        canvas.setGeometry(0, 73, 674, 791);
        auto *owner = new QWidget;
        auto *panel = new SearchPanel(owner, test::hostReaderServices().keyboard);
        canvas.setContent(owner);
        canvas.viewportResized = [&] {
            canvas.fitContent();
            panel->setGeometry(owner->rect());
        };
        canvas.viewportResized();
        window.show();
        qInfo() << "Canvas device pixel ratio:" << canvas.devicePixelRatioF();
        panel->open(false, 0);
        auto *query = panel->findChild<QLineEdit *>("rrSearchQuery");
        QVERIFY(query);
        QTRY_VERIFY(query->hasFocus());
        const auto editorRectangle = [&] {
            const QRectF sceneRect(query->mapTo(owner, QPoint()), query->size());
            return canvas.viewportTransform().mapRect(sceneRect).translated(
                canvas.viewport()->mapTo(&window, QPoint()));
        };
        const auto cachedRectangle = [] {
            auto *input = QGuiApplication::inputMethod();
            return input->inputItemTransform().mapRect(input->inputItemRectangle());
        };
        QInputMethodQueryEvent queryEvent(Qt::ImInputItemClipRectangle);
        QApplication::sendEvent(&canvas, &queryEvent);
        QCOMPARE(cachedRectangle(), editorRectangle());

        canvas.resize(620, 700);
        QCoreApplication::processEvents();
        QVERIFY(query->hasFocus());
        QCOMPARE(cachedRectangle(), editorRectangle());

        QLineEdit otherEditor(&window);
        otherEditor.setGeometry(20, 10, 300, 40);
        otherEditor.show();
        otherEditor.setFocus();
        QTRY_VERIFY(otherEditor.hasFocus());
        const QRectF otherRectangle = cachedRectangle();
        QInputMethodQueryEvent inactiveQuery(Qt::ImInputItemClipRectangle);
        QApplication::sendEvent(&canvas, &inactiveQuery);
        QCOMPARE(cachedRectangle(), otherRectangle);

        auto *foreignEditor = new QLineEdit;
        foreignEditor->resize(300, 40);
        auto *foreignProxy = canvas.scene()->addWidget(foreignEditor);
        foreignProxy->setPos(40, 300);
        canvas.setFocus();
        foreignEditor->setFocus();
        QTRY_VERIFY(foreignProxy->hasFocus());
        QVERIFY(!owner->graphicsProxyWidget()->hasFocus());
        QVERIFY(QGuiApplication::focusObject() == &canvas || QGuiApplication::focusObject() == canvas.viewport());
        QVERIFY(QInputMethod::queryFocusObject(Qt::ImEnabled, {}).toBool());
        const QRectF foreignSceneRect = foreignProxy->mapRectToScene(QRectF(foreignEditor->rect()));
        const QRectF foreignRectangle = canvas.viewportTransform().mapRect(foreignSceneRect);
        const QPoint offset = canvas.viewport()->mapTo(&window, QPoint());
        auto *input = QGuiApplication::inputMethod();
        input->setInputItemRectangle(foreignRectangle);
        const QTransform foreignTransform = QTransform::fromTranslate(offset.x(), offset.y());
        input->setInputItemTransform(foreignTransform);
        QInputMethodQueryEvent foreignQuery(Qt::ImInputItemClipRectangle);
        QApplication::sendEvent(&canvas, &foreignQuery);
        QCOMPARE(input->inputItemRectangle(), foreignRectangle);
        QCOMPARE(input->inputItemTransform(), foreignTransform);
    }
    void lastBookRequiresAnExplicitTap() {
        android::StartScreen screen(presentation());
        screen.resize(360, 800);
        screen.show();
        auto *card = screen.findChild<QPushButton *>("androidLastBook");
        QVERIFY(card);
        QVERIFY(!card->isVisible());
        QSignalSpy reopen(&screen, &android::StartScreen::reopenRequested);
        screen.setLastBook(
            {{"path", "/book.epub"}, {"title", "A long book title with several words"}, {"author", "An author"}});
        QCoreApplication::processEvents();
        QVERIFY(card->isVisible());
        QCOMPARE(reopen.count(), 0);
        QTest::mouseClick(card, Qt::LeftButton);
        QCOMPARE(reopen.count(), 1);
        QCOMPARE(reopen.first().first().toString(), QString("/book.epub"));
        screen.showOpening();
        QVERIFY(!card->isVisible());
        screen.showReady();
        QVERIFY(card->isVisible());
        screen.setLastBook({});
        QVERIFY(!card->isVisible());
    }
    void lastBookLongPressRequiresConfirmation() {
        android::StartScreen screen(presentation());
        screen.resize(674, 896);
        const QJsonObject last{{"path", "/book.epub"}, {"title", "A book"}};
        screen.setLastBook(last);
        screen.show();
        QTest::qWait(30);
        auto *card = screen.findChild<QPushButton *>("androidLastBook");
        QVERIFY(card);
        QSignalSpy reopen(&screen, &android::StartScreen::reopenRequested);
        QSignalSpy forget(&screen, &android::StartScreen::forgetRequested);
        auto hold = [&] {
            QTest::mousePress(card, Qt::LeftButton);
            QTest::qWait(650);
            QTest::mouseRelease(card, Qt::LeftButton);
        };
        hold();
        QCOMPARE(reopen.count(), 0);
        QCOMPARE(forget.count(), 0);
        auto *canvas = screen.findChild<android::WidgetCanvas *>("androidRecentModalCanvas");
        QVERIFY(canvas);
        auto *confirm = canvas->content->findChild<QPushButton *>("androidForgetConfirm");
        auto *cancel = canvas->content->findChild<QPushButton *>("androidForgetCancel");
        QVERIFY(confirm);
        QVERIFY(cancel);
        QVERIFY(canvas->isVisible());
        QVERIFY(confirm->isVisible());
        QTest::qWait(100);
        QVERIFY(confirm->updatesEnabled());
        QCOMPARE(confirm->parentWidget()->palette().color(QPalette::Window), QColor(Qt::white));
        const auto rendered = canvas->viewport()->grab().toImage();
        const auto cardTop = canvas->mapFromScene(confirm->parentWidget()->geometry().topLeft());
        QCOMPARE(rendered.pixelColor(cardTop * rendered.devicePixelRatio()), QColor(Qt::black));
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!canvas->isVisible());
        QVERIFY(card->isVisible());
        hold();
        QTest::mouseClick(confirm, Qt::LeftButton);
        QCOMPARE(forget.count(), 1);
        QCOMPARE(reopen.count(), 0);
        screen.showForgetError();
        QVERIFY(canvas->isVisible());
        QCOMPARE(canvas->content->findChild<QLabel *>("rrModalDescription")->textFormat(), Qt::PlainText);
        screen.setLastBook({});
        QVERIFY(!card->isVisible());
        QVERIFY(!canvas->isVisible());
    }
    void draggingLastBookDoesNotAskToRemove() {
        android::StartScreen screen(presentation());
        screen.resize(674, 896);
        screen.setLastBook({{"path", "/book.epub"}, {"title", "A book"}});
        screen.show();
        auto *card = screen.findChild<QPushButton *>("androidLastBook");
        QVERIFY(card);
        QTest::mousePress(card, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QTest::mouseMove(card, QPoint(20, 60));
        QTest::qWait(650);
        QTest::mouseRelease(card, Qt::LeftButton, Qt::NoModifier, QPoint(20, 60));
        QVERIFY(!screen.findChild<android::WidgetCanvas *>("androidRecentModalCanvas"));
    }
    void lastBookPaintsCoverAndStandardButtonEdges() {
        android::StartScreen screen(presentation());
        screen.resize(360, 800);
        QImage cover(60, 90, QImage::Format_RGB32);
        cover.fill(QColor(0, 192, 0));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(cover.save(&buffer, "PNG"));
        screen.setLastBook(
            {{"path", "/book.epub"}, {"title", "A book"}, {"cover", QString::fromLatin1(bytes.toBase64())}});
        screen.show();
        QCoreApplication::processEvents();
        auto *card = screen.findChild<QPushButton *>("androidLastBook");
        QVERIFY(card);
        auto *exit = screen.findChild<QPushButton *>("androidExit");
        QVERIFY(exit);
        for (bool pressed : {false, true}) {
            card->setDown(pressed);
            exit->setDown(pressed);
            const auto frame = card->grab().toImage();
            const auto standard = exit->grab().toImage();
            const auto ratio = frame.devicePixelRatio();
            QCOMPARE(frame.pixelColor(qRound(40 * ratio), qRound(card->height() / 2 * ratio)), QColor(0, 192, 0));
            for (const auto point :
                 {QPoint(10, 0), QPoint(0, 10), QPoint(10, card->height() - 1), QPoint(card->width() - 1, 10)})
                QCOMPARE(frame.pixelColor(point * ratio), QColor(Qt::black));
            QCOMPARE(frame.pixelColor(QPoint(8, 8) * ratio), standard.pixelColor(QPoint(8, 8) * ratio));
        }
    }
    void openingHidesActionsAndErrorRestoresThem() {
        android::StartScreen screen(presentation());
        screen.resize(674, 896);
        screen.show();
        auto *open = screen.findChild<QPushButton *>("androidOpenBook");
        auto *exit = screen.findChild<QPushButton *>("androidExit");
        auto *detail = screen.findChild<QLabel *>("androidStartDetail");
        QVERIFY(open);
        QVERIFY(exit);
        QVERIFY(detail);
        QSignalSpy opens(&screen, &android::StartScreen::openRequested);
        QSignalSpy exits(&screen, &android::StartScreen::exitRequested);
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(opens.count(), 1);
        screen.showOpening();
        QVERIFY(!open->isVisible());
        QVERIFY(!exit->isVisible());

        const QString error = "Cannot read <book>.epub & its resources.";
        screen.showError(error);
        QVERIFY(open->isEnabled());
        QVERIFY(open->isVisible());
        QVERIFY(exit->isVisible());
        QTest::mouseClick(exit, Qt::LeftButton);
        QCOMPARE(exits.count(), 1);
        QCOMPARE(detail->text(), error);
        QCOMPARE(detail->textFormat(), Qt::PlainText);
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(opens.count(), 2);
    }
    void openingMatchesKoboAndResetsBetweenBooks() {
        android::StartScreen screen(presentation());
        screen.resize(1072, 1448);
        screen.show();
        screen.showOpening();
        QTest::qWait(30);
        auto *loading = screen.currentWidget();
        QVERIFY(!loading->isWindow());
        screen.hide();
        QVERIFY(!loading->isVisible());
        screen.show();
        for (auto *button : loading->findChildren<QPushButton *>())
            QVERIFY(!button->isVisible());
        auto *progress = loading->findChild<QProgressBar *>("rrOpeningProgress");
        QVERIFY(progress);
        screen.setProgress(40);
        QCOMPARE(progress->value(), 40);
        screen.setProgress(20);
        QCOMPARE(progress->value(), 40);
        auto kobo = createOpening(nullptr, {}, presentation());
        kobo->window()->resize(screen.size());
        kobo->window()->findChild<QPushButton *>("rrCancelOpening")->hide();
        kobo->setProgress(40);
        kobo->window()->show();
        QTest::qWait(30);
        const auto frame = loading->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QCOMPARE(frame, kobo->window()->grab().toImage().convertToFormat(QImage::Format_RGB32));
        QSignalSpy exits(&screen, &android::StartScreen::exitRequested);
        QTest::keyClick(loading, Qt::Key_Escape);
        QCOMPARE(exits.count(), 1);
        QVERIFY(loading->isVisible());
        screen.showError("Invalid book");
        screen.showOpening();
        QTest::qWait(30);
        progress = screen.currentWidget()->findChild<QProgressBar *>("rrOpeningProgress");
        QCOMPARE(progress->value(), 0);
        QVERIFY(!screen.currentWidget()->findChild<QLabel *>("rrOpeningHint")->isVisible());
        for (auto *button : screen.findChildren<QPushButton *>())
            QVERIFY(!button->isVisible());
        PaintRegions paints;
        screen.currentWidget()->installEventFilter(&paints);
        screen.setProgress(0);
        QTest::qWait(100);
        QCOMPARE(paints.count, 0);
    }
    void portraitLandscapeAndSmallWindowsRemainReachable() {
        android::StartScreen screen(presentation());
        screen.show();
        for (const auto size : {QSize(674, 896), QSize(896, 674), QSize(360, 480)}) {
            screen.resize(size);
            screen.showError(QString(300, 'W'));
            QTest::qWait(30);
            auto *exit = screen.findChild<QPushButton *>("androidExit");
            auto *open = screen.findChild<QPushButton *>("androidOpenBook");
            QCOMPARE(screen.findChild<QScrollArea *>("androidIntro")->horizontalScrollBar()->maximum(), 0);
            QVERIFY(open->height() >= 48);
            QVERIFY(exit->height() >= 48);
            screen.findChild<QScrollArea *>("androidIntro")->ensureWidgetVisible(exit);
            QTest::qWait(20);
            const QRect bounds(exit->mapTo(screen.findChild<QScrollArea *>("androidIntro")->viewport(), QPoint()),
                               exit->size());
            QVERIFY(screen.findChild<QScrollArea *>("androidIntro")->viewport()->rect().contains(bounds));
        }
    }
    void separateDamageDoesNotRepaintThePageBetween_data() {
        QTest::addColumn<double>("scale");
        QTest::newRow("native-pixels") << 1.0;
        QTest::newRow("android-density") << (1.0 / 1.875);
    }
    void separateDamageDoesNotRepaintThePageBetween() {
        QFETCH(double, scale);
        android::WidgetCanvas canvas(nullptr);
        canvas.resize(500, 700);
        auto *page = new QWidget;
        page->resize(500, 700);
        page->setAutoFillBackground(true);
        canvas.content = page;
        canvas.setContent(page);
        canvas.setSceneRect(0, 0, 500, 700);
        canvas.setTransform(QTransform::fromScale(scale, scale));
        PaintRegions paints;
        page->installEventFilter(&paints);
        canvas.show();
        QTest::qWait(100);
        paints.clear();
        page->update(QRect(20, 20, 40, 30));
        page->update(QRect(400, 600, 40, 30));
        QTRY_VERIFY(paints.count > 0);
        QVERIFY(!paints.painted.contains(QPoint(250, 350)));
        QVERIFY(paints.painted.rectCount() >= 2);
        QTest::qWait(100);
        paints.clear();
        QTest::qWait(150);
        QCOMPARE(paints.count, 0);
        page->update();
        QTRY_VERIFY(paints.painted.contains(QPoint(250, 350)));
        QTest::qWait(100);
        paints.clear();
        page->resize(550, 750);
        canvas.setSceneRect(0, 0, 550, 750);
        canvas.resize(550, 750);
        QTRY_VERIFY(paints.painted.contains(QPoint(520, 720)));
        QPalette palette = page->palette();
        palette.setColor(QPalette::Window, Qt::black);
        page->setPalette(palette);
        QTest::qWait(100);
        const QImage frame = canvas.viewport()->grab().toImage();
        QCOMPARE(frame.pixelColor(QPoint(int(250 * scale), int(350 * scale)) * frame.devicePixelRatio()),
                 QColor(Qt::black));
    }
};
QTEST_MAIN(AndroidUiTest)
#include "android_ui_test.moc"
