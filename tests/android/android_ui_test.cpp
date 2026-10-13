#include "interfaces/reader/ui/controls/sliders.h"
#include "platform/android/start_screen.h"
#include "platform/android/canvas.h"
#include "interfaces/reader/views/opening.h"
#include <QScrollArea>
#include <QBuffer>
#include <QProgressBar>
#include "assets/assets.h"
#include "interfaces/reader/ui/style/fonts.h"
#include <QApplication>
#include <QLabel>
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
        auto *canvas = dynamic_cast<android::WidgetCanvas *>(screen.findChild<QWidget *>("androidRecentModalCanvas"));
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
        QVERIFY(!dynamic_cast<android::WidgetCanvas *>(screen.findChild<QWidget *>("androidRecentModalCanvas")));
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
