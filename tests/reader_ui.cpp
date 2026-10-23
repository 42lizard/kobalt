#include "support/paint_regions.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/loading.h"
#include "interfaces/reader/ui/controls/sliders.h"
#include "interfaces/reader/ui/dialogs/modals.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/views/opening.h"
#include "interfaces/reader/views/reading_page.h"
#include "platforms/android/canvas.h"
#include "platforms/kobo/display/annotation_damage.h"
#include "support/host_presentation.h"
#include "support/reader_inputs.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace kobalt;
namespace {
using test::PaintRegions;
class DeleteOnTouchCancel final : public QPushButton {
public:
    using QPushButton::QPushButton;
    bool event(QEvent *event) override {
        if (event->type() == QEvent::TouchCancel) {
            delete this;
            return true;
        }
        return QPushButton::event(event);
    }
};
class Canvas final : public android::WidgetCanvas {
public:
    Canvas(QWidget *surface, QSize size) : WidgetCanvas(nullptr) {
        surface->setFixedSize(size);
        setContent(surface);
        setSceneRect(QRectF(QPointF(), size));
    }
    void resizeEvent(QResizeEvent *event) override {
        WidgetCanvas::resizeEvent(event);
        fitInView(sceneRect(), Qt::KeepAspectRatio);
    }
};
bool clickAt(Canvas &canvas, QPoint point) {
    QCoreApplication::processEvents();
    const auto local = canvas.mapFromScene(point);
    if (!canvas.viewport()->rect().contains(local))
        return false;
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, local);
    return true;
}
bool click(Canvas &canvas, QWidget *widget) {
    QCoreApplication::processEvents();
    if (!widget || !widget->isVisible() || !widget->isEnabled())
        return false;
    const auto point = widget->mapTo(canvas.content, widget->rect().center());
    const auto *receiver = canvas.content->childAt(point);
    if (receiver != widget && (!receiver || !widget->isAncestorOf(receiver)))
        return false;
    return clickAt(canvas, point);
}
void variants() {
    QTest::addColumn<bool>("dark");
    QTest::addColumn<int>("ppi");
    for (bool dark : {false, true})
        for (int ppi : {300, 212}) {
            const auto name = QString("%1-%2").arg(dark ? "dark" : "light").arg(ppi);
            QTest::newRow(qPrintable(name)) << dark << ppi;
        }
}
}
class ReaderUiTests final : public QObject {
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
    void canvasInputRespectsModalBarrier() {
        auto *surface = new QWidget;
        surface->resize(420, 600);
        auto *button = new ui::Button("Underlying action", surface);
        button->setGeometry(20, 20, 180, 60);
        QSignalSpy activated(button, &QPushButton::clicked);
        auto *overlay = new ui::NoticeModal(surface, {"Modal input barrier", "Notice", {}}, {"OK", "testModalConfirm"});
        int dismissals = 0;
        overlay->dismissed = [&] {
            ++dismissals;
        };
        Canvas canvas(surface, QSize(420, 600));
        canvas.resize(420, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(click(canvas, button));
        QCOMPARE(activated.count(), 1);
        button->setEnabled(false);
        QVERIFY(!click(canvas, button));
        QVERIFY(clickAt(canvas, button->geometry().center()));
        QCOMPARE(activated.count(), 1);
        button->setEnabled(true);
        button->hide();
        QVERIFY(!click(canvas, button));
        button->show();
        for (bool dark : {false, true}) {
            surface->setProperty("reader_dark", dark);
            auto colors = surface->palette();
            colors.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
            colors.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
            surface->setPalette(colors);
            overlay->fitCard();
            overlay->show();
            overlay->raise();
            QTRY_COMPARE(overlay->geometry(), surface->rect());
            QVERIFY(overlay->rect().contains(overlay->card()->geometry()));
            QVERIFY(!click(canvas, button));
            QCOMPARE(activated.count(), 1);
            const int before = dismissals;
            QVERIFY(clickAt(canvas, button->geometry().center()));
            QCOMPARE(dismissals, before + 1);
            QCOMPARE(activated.count(), 1);
            surface->setFixedSize(dark ? QSize(420, 600) : QSize(600, 420));
            QTRY_COMPARE(overlay->geometry(), surface->rect());
            QVERIFY(overlay->rect().contains(overlay->card()->geometry()));
            QVERIFY((overlay->card()->geometry().center() - overlay->rect().center()).manhattanLength() <= 2);
            QVERIFY(!click(canvas, button));
            QCOMPARE(activated.count(), 1);
            overlay->hide();
        }
        QVERIFY(click(canvas, button));
        QCOMPARE(activated.count(), 2);
    }
    void delayedProgressPaintsAfterTheEventLoop() {
        QWidget owner;
        owner.resize(600, 800);
        owner.show();
        ui::ModalOverlay loading(&owner);
        loading.card()->message("Applying appearance changes", "rrUpdatingText");
        loading.fitCard();
        QVERIFY(loading.progressUpdateRect(35, "Applying appearance changes").isEmpty());
        loading.setDelayedProgress(true, 1100, 20, "Applying appearance changes");
        QCoreApplication::processEvents();
        auto *bar = dynamic_cast<ui::ProgressBar *>(loading.findChild<QWidget *>("rrApplyingProgress"));
        QVERIFY(bar);
        QVERIFY(bar->isVisible());
        QCOMPARE(loading.progressUpdateRect(35, "Applying appearance changes"),
                 QRect(bar->mapTo(loading.window(), QPoint()), bar->size()));
        QVERIFY(loading.progressUpdateRect(20, "Applying appearance changes").isEmpty());
        QVERIFY(loading.progressUpdateRect(35, "Updating page layout").isEmpty());
        PaintRegions paints;
        bar->installEventFilter(&paints);
        PaintRegions ownerPaints;
        owner.installEventFilter(&ownerPaints);

        loading.setDelayedProgress(true, 1100, 35, "Applying appearance changes");
        QCOMPARE(bar->value(), 35);
        QCOMPARE(paints.count, 0);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
        QVERIFY(paints.painted.boundingRect().width() < bar->width() / 2);
        QVERIFY((ownerPaints.painted - QRegion(QRect(bar->mapTo(&owner, QPoint()), bar->size()))).isEmpty());

        paints.count = 0;
        paints.painted = {};
        loading.setDelayedProgress(true, 1100, 35, "Applying appearance changes");
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);

        loading.setDelayedProgress(true, 1100, 50, "Applying appearance changes");
        loading.setDelayedProgress(true, 1100, 65, "Applying appearance changes");
        QCOMPARE(bar->value(), 65);
        QCOMPARE(paints.count, 0);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);

        owner.setUpdatesEnabled(false);
        loading.setDelayedProgress(true, 1100, 80, "Applying appearance changes");
        owner.setUpdatesEnabled(true);
        QVERIFY(bar->updatesEnabled());
        QCoreApplication::processEvents();

        paints.count = 0;
        loading.setDelayedProgress(false, 1100, 100);
        QCOMPARE(bar->value(), 0);
        QVERIFY(!bar->isVisible());
        QCOMPARE(paints.count, 0);
        QVERIFY(loading.progressUpdateRect(35, "Applying appearance changes").isEmpty());
    }
    void progressPaintsOnlyChangedPixels_data() {
        QTest::addColumn<int>("ppi");
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("rtl");
        for (const int ppi : {300, 212}) {
            for (const bool dark : {false, true}) {
                for (const bool rtl : {false, true}) {
                    const auto name =
                        QString("%1-%2-%3").arg(ppi).arg(dark ? "dark" : "light").arg(rtl ? "rtl" : "ltr");
                    QTest::newRow(qPrintable(name)) << ppi << dark << rtl;
                }
            }
        }
    }
    void progressPaintsOnlyChangedPixels() {
        QFETCH(int, ppi);
        QFETCH(bool, dark);
        QFETCH(bool, rtl);
        struct RestoreDensity {
            int value = ui::density();
            ~RestoreDensity() {
                ui::setDensity(value);
            }
        } restore;
        ui::setDensity(ppi);
        QWidget owner;
        owner.resize(600, 800);
        ui::ProgressBar bar(&owner);
        bar.resize(ui::px(472), bar.height());
        bar.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
        auto palette = bar.palette();
        palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        bar.setPalette(palette);
        owner.show();
        QCoreApplication::processEvents();
        PaintRegions paints, ownerPaints;
        bar.installEventFilter(&paints);
        owner.installEventFilter(&ownerPaints);
        for (const int next : {20, 35, 65, 40, 0, 100}) {
            const auto before = bar.grab().toImage();
            paints.painted = {};
            paints.count = ownerPaints.count = 0;
            bar.setValue(next);
            QCOMPARE(paints.count, 0);
            QCoreApplication::processEvents();
            QCOMPARE(paints.count, 1);
            QCOMPARE(ownerPaints.count, 0);
            const auto damage = paints.painted;
            const auto after = bar.grab().toImage();
            QRect changed;
            for (int y = 0; y < before.height(); ++y) {
                for (int x = 0; x < before.width(); ++x) {
                    if (before.pixel(x, y) != after.pixel(x, y)) {
                        changed = changed.united(QRect(x, y, 1, 1));
                    }
                }
            }
            QVERIFY(!changed.isEmpty());
            QCOMPARE(damage, QRegion(changed));
            const int border = ui::metrics::control::border;
            QVERIFY(bar.rect().adjusted(border, border, -border, -border).contains(changed));
        }
        paints.count = 0;
        bar.setValue(200);
        QCoreApplication::processEvents();
        QCOMPARE(bar.value(), 100);
        QCOMPARE(paints.count, 0);
        bar.setValue(-1);
        QCoreApplication::processEvents();
        QCOMPARE(bar.value(), 0);
        bar.resize(2 * int(ui::metrics::control::border) + 12, bar.height());
        QCoreApplication::processEvents();
        paints.count = 0;
        bar.setValue(1);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
        bar.setValue(9);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
    }
    void openingProgressPaintsAfterTheEventLoop() {
        QWidget owner;
        owner.resize(600, 800);
        auto opening = createEmbeddedOpening(&owner, test::makeHostPresentation);
        opening->window()->setGeometry(owner.rect());
        owner.show();
        opening->window()->show();
        QCoreApplication::processEvents();
        auto *bar = opening->window()->findChild<QProgressBar *>("rrOpeningProgress");
        QVERIFY(bar);
        QVERIFY(bar->isVisible());
        PaintRegions paints;
        bar->installEventFilter(&paints);
        opening->setProgress(20);
        opening->setProgress(40);
        QCOMPARE(bar->value(), 40);
        QCOMPARE(paints.count, 0);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
        paints.count = 0;
        opening->setProgress(40);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
        opening->setProgress(60);
        opening->setProgress(100);
        opening->window()->hide();
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
    }
    void openingFirstPaintCallback() {
        QWidget owner;
        owner.resize(600, 800);
        auto opening = createEmbeddedOpening(&owner, test::makeHostPresentation);
        opening->window()->setGeometry(owner.rect());
        int calls = 0;
        opening->afterFirstPaint([&] {
            ++calls;
        });
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
        owner.show();
        opening->window()->show();
        QTRY_COMPARE(calls, 1);
        opening->window()->update();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);

        QWidget otherOwner;
        otherOwner.resize(600, 800);
        auto discarded = createEmbeddedOpening(&otherOwner, test::makeHostPresentation);
        discarded->afterFirstPaint([&] {
            ++calls;
        });
        discarded.reset();
        otherOwner.show();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
    }
    void readingStripChangesLeaveBodyUnpainted() {
        ReadingPage page(nullptr, [] {});
        page.resize(400, 600);
        page.image = QImage(400, 480, QImage::Format_RGB32);
        page.image.fill(Qt::white);
        page.slotContents[0].text = "09:00";
        page.slotContents[4].text = "1 / 10";
        page.refreshContent();
        page.show();
        QCoreApplication::processEvents();
        PaintRegions paints;
        page.installEventFilter(&paints);
        page.slotContents[0].text = "09:01";
        page.slotContents[4].text = "2 / 10";
        page.refreshContent();
        QCoreApplication::processEvents();
        QVERIFY(!paints.painted.isEmpty());
        QVERIFY(!paints.painted.intersects(
            QRect(0, page.topHeight(), page.width(), page.height() - page.topHeight() - page.bottomHeight())));
        QVERIFY(!page.property("reader_page_turn").toBool());
        paints.painted = {};
        page.refreshContent();
        QCoreApplication::processEvents();
        QVERIFY(paints.painted.isEmpty());
    }
    void annotationPaintPreservesText_data() {
        variants();
    }
    void annotationPaintPreservesText() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        const int density = ui::density();
        struct Restore {
            int density;
            ~Restore() {
                ui::setDensity(density);
            }
        } restore{density};
        ui::setDensity(ppi);
        ReadingPage page(nullptr, [] {});
        page.resize(500, 600);
        page.show();
        QCoreApplication::processEvents();
        page.image = QImage(500, 500, QImage::Format_RGB32);
        const QRect passage(100, 100, 200, 60);
        page.setProperty("reader_dark", dark);
        QPalette palette = page.palette();
        palette.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
        palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        page.setPalette(palette);
        page.image.fill(dark ? Qt::black : Qt::white);
        const QPoint interior = passage.center() + QPoint(0, page.topHeight());
        page.image.setPixelColor(passage.center(), dark ? Qt::white : Qt::black);
        page.refreshContent();
        page.annotate({}, {});
        const QImage plain = page.snapshot();
        page.annotate({passage}, {});
        const QImage draft = page.snapshot();
        QCOMPARE(draft.pixelColor(interior), plain.pixelColor(interior));
        const int y = passage.top() + page.topHeight();
        const int x = passage.center().x();
        const int baseline = passage.bottom() - ui::px(2) + page.topHeight();
        const int radius = (ui::px(4) + 1) / 2;
        const QRect dotBand(passage.left() - radius - 1, baseline - radius - 1, passage.width() + 2 * radius + 2,
                            2 * radius + 3);
        // Every changed pixel must be on the dotted baseline. In particular,
        // neither terminal edge may contain a full-height bracket stroke.
        for (int row = 0; row < draft.height(); ++row) {
            for (int col = 0; col < draft.width(); ++col) {
                if (!dotBand.contains(col, row)) {
                    QCOMPARE(draft.pixelColor(col, row), plain.pixelColor(col, row));
                }
            }
        }
        int islands = 0, gaps = 0;
        bool inkBefore = false;
        for (int col = passage.left(); col <= passage.right(); ++col) {
            const bool ink = draft.pixelColor(col, baseline) != plain.pixelColor(col, baseline);
            if (ink && !inkBefore) {
                ++islands;
            }
            if (!ink) {
                ++gaps;
            }
            inkBefore = ink;
        }
        QVERIFY(islands >= 10);
        QVERIFY(gaps > passage.width() / 3);
        QCoreApplication::processEvents();
        PaintRegions repairs;
        page.installEventFilter(&repairs);
        page.annotate({passage}, {});
        QCoreApplication::processEvents();
        QCOMPARE(repairs.count, 0);
        const QRect longer = passage.adjusted(0, 0, 40, 0);
        page.annotate({longer}, {});
        QCoreApplication::processEvents();
        const QImage preview = page.snapshot();
        const QPoint previewExtension(passage.right() + 30, baseline);
        QVERIFY(repairs.count > 0);
        QVERIFY(repairs.painted.contains(previewExtension));
        QVERIFY(preview != draft);
        repairs.painted = {};
        repairs.count = 0;
        page.annotate({passage}, {});
        QCoreApplication::processEvents();
        QVERIFY(repairs.count > 0);
        QVERIFY(repairs.painted.contains(previewExtension));
        QCOMPARE(page.snapshot(), draft);
        page.annotate({passage}, {}, {QRect(320, 220, 80, 30)});
        QVERIFY(page.snapshot() != draft);
        page.annotate({passage}, {});
        QCOMPARE(page.snapshot(), draft);
        page.annotate({}, {passage});
        const QImage saved = page.snapshot();
        const QColor gray = saved.pixelColor(x + 10, y + 10);
        QVERIFY(gray.red() > 0 && gray.red() < 255);
        QCOMPARE(gray.red(), gray.green());
        QCOMPARE(gray.green(), gray.blue());
        QCOMPARE(saved.pixelColor(interior), plain.pixelColor(interior));
        page.setAnnotationColors({qMakePair(QVector<QRect>{passage, passage}, QString("yellow"))});
        const QImage yellow = page.snapshot();
        QCOMPARE(yellow.pixelColor(interior), plain.pixelColor(interior));
        QVERIFY(yellow.pixelColor(x + 10, y + 10) != gray);
        QVERIFY(page.property("reader_colour_regions").toBool());
        page.setAnnotationColors({qMakePair(QVector<QRect>{passage}, QString("blue"))});
        const QImage blue = page.snapshot();
        QCOMPARE(blue.pixelColor(interior), plain.pixelColor(interior));
        QVERIFY(blue.pixelColor(x + 10, y + 10) != yellow.pixelColor(x + 10, y + 10));
        page.setAnnotationColors({qMakePair(QVector<QRect>{passage}, QString("yellow")),
                                  qMakePair(QVector<QRect>{passage}, QString("gray"))});
        QCOMPARE(page.snapshot(), saved);
        QVERIFY(!page.property("reader_colour_regions").toBool());
        QVERIFY(!page.property("reader_annotation_repairs").value<QRegion>().isEmpty());
        page.annotate({}, {});
        QCOMPARE(page.snapshot(), plain);
        QVERIFY(page.property("reader_annotation_regions").value<QRegion>().isEmpty());
        const auto outgoing = annotationRepairRegions(&page);
        QVERIFY(!outgoing.isEmpty());
        consumeAnnotationRepairs(&page, outgoing);
        QVERIFY(annotationRepairRegions(&page).isEmpty());
    }
    void annotationDamageBatchesLogicalUpdate_data() {
        variants();
    }
    void annotationDamageBatchesLogicalUpdate() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        const int originalDensity = ui::density();
        struct Restore {
            int value;
            ~Restore() {
                ui::setDensity(value);
            }
        } restore{originalDensity};
        ui::setDensity(ppi);
        ReadingPage page(nullptr, [] {});
        page.resize(600, 800);
        page.setProperty("reader_dark", dark);
        page.image = QImage(600, 700, QImage::Format_RGB32);
        page.image.fill(dark ? Qt::black : Qt::white);
        page.refreshContent();
        page.show();
        QCoreApplication::processEvents();
        PaintRegions paints;
        page.installEventFilter(&paints);
        QVector<QRect> lines;
        for (int i = 0; i < 6; ++i) {
            lines.append(QRect(30, 70 + i * 45, 480 - i * 20, 25));
        }
        page.annotate(lines, {});
        QTRY_VERIFY(paints.count > 0);
        QCOMPARE(paints.count, 1);
        QCOMPARE(paints.painted.rectCount(), 1);
        QVERIFY(paints.painted.contains(lines.first().translated(0, page.topHeight())));
        QVERIFY(paints.painted.contains(lines.last().translated(0, page.topHeight())));
        const QVector<QPair<QVector<QRect>, QString>> blue{{lines, "blue"}};
        const QVector<QPair<QVector<QRect>, QString>> pink{{lines.mid(0, 3), "pink"}, {lines.mid(3), "green"}};
        const QVector<std::function<void()>> changes{[&] {
                                                         page.annotate(lines.mid(0, 2), {}, lines.mid(3));
                                                     },
                                                     [&] {
                                                         page.annotate(lines, {});
                                                     },
                                                     [&] {
                                                         page.annotate({}, lines);
                                                     },
                                                     [&] {
                                                         page.setAnnotationColors(blue);
                                                     },
                                                     [&] {
                                                         page.setAnnotationColors(pink);
                                                     },
                                                     [&] {
                                                         page.annotate(lines.mid(1, 4), lines);
                                                     },
                                                     [&] {
                                                         page.annotate({}, lines);
                                                     },
                                                     [&] {
                                                         page.setAnnotationColors({});
                                                         page.annotate({}, {});
                                                     }};
        for (const auto &change : changes) {
            paints.count = 0;
            paints.painted = {};
            change();
            QTRY_VERIFY(paints.count > 0);
            QCOMPARE(paints.count, 1);
            QCOMPARE(paints.painted.rectCount(), 1);
            QVERIFY(paints.painted.boundingRect().width() < page.width());
            QVERIFY(paints.painted.contains(lines.first().translated(0, page.topHeight())));
            QVERIFY(paints.painted.contains(lines.last().translated(0, page.topHeight())));
        }
        paints.count = 0;
        page.annotate({}, {});
        page.setAnnotationColors({});
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
    }
    void annotationFrameDamageJoinsRawDamage_data() {
        variants();
    }
    void annotationFrameDamageJoinsRawDamage() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        const int density = ui::density();
        struct Restore {
            int density;
            ~Restore() {
                ui::setDensity(density);
            }
        } restore{density};
        ui::setDensity(ppi);
        ReadingPage page(nullptr, [] {});
        page.resize(600, 800);
        page.setProperty("reader_dark", dark);
        page.image = QImage(600, 700, QImage::Format_RGB32);
        page.image.fill(dark ? Qt::black : Qt::white);
        page.refreshContent();
        page.show();
        QCoreApplication::processEvents();
        const QImage plain = page.snapshot();
        PaintRegions paints;
        page.installEventFilter(&paints);
        const QVector<QRect> lines{{30, 70, 450, 25}, {30, 300, 420, 25}};
        const QVector<QPair<QVector<QRect>, QString>> blue{{lines, "blue"}};
        page.installAnnotationFrame({}, blue);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
        page.preparedFrom = page.image.cacheKey();
        page.preparedDamage = {};
        page.refreshContent(true, true);
        QCOMPARE(paints.count, 1);
        QVERIFY(paints.painted.contains(lines.first().translated(0, page.topHeight())));
        QVERIFY(paints.painted.contains(lines.last().translated(0, page.topHeight())));
        QVERIFY(page.snapshot() != plain);
        QVERIFY(page.window()->property("reader_colour_regions").toBool());
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
        paints.count = 0;
        paints.painted = {};
        page.installAnnotationFrame(lines, {});
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 0);
        page.preparedFrom = page.image.cacheKey();
        page.preparedDamage = QRect(520, 500, 20, 20);
        page.refreshContent(true, true);
        QCOMPARE(paints.count, 1);
        QVERIFY(paints.painted.contains(QRect(520, 500, 20, 20).translated(0, page.topHeight())));
        QVERIFY(paints.painted.contains(lines.last().translated(0, page.topHeight())));
        QVERIFY(!page.window()->property("reader_colour_regions").toBool());
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
        paints.count = 0;
        page.installAnnotationFrame({}, {});
        page.refreshContent(true, true);
        QCOMPARE(paints.count, 1);
        QCOMPARE(page.snapshot(), plain);
        QCoreApplication::processEvents();
        QCOMPARE(paints.count, 1);
    }
    void annotationContourKeepsParagraphGaps() {
        const QVector<QRect> lines{QRect(100, 100, 200, 30), QRect(100, 138, 180, 30), QRect(100, 210, 200, 30)};
        const auto contour = ReadingPage::annotationContour(lines);
        QVERIFY(contour.contains(QPoint(160, 133)));
        QVERIFY(!contour.contains(QPoint(160, 188)));
        QVERIFY(!contour.contains(QPoint(90, 133)));
        QVERIFY(contour.contains(QPoint(160, 220)));
        QVector<QRect> vertical;
        for (const auto &line : lines) {
            vertical.append(QRect(line.y(), line.x(), line.height(), line.width()));
        }
        const auto verticalContour = ReadingPage::annotationContour(vertical, true);
        QVERIFY(verticalContour.contains(QPoint(133, 160)));
        QVERIFY(!verticalContour.contains(QPoint(188, 160)));
        // This is the measured first vertical glyph cell (69x46), not a tall column.
        // Its explicit writing axis must win over the rectangle's aspect ratio.
        const auto shortVertical = ReadingPage::annotationUnderline({QRect(483, 24, 69, 46)}, true);
        QVERIFY(shortVertical.boundingRect().height() > shortVertical.boundingRect().width() * 5);
        QVERIFY(shortVertical.boundingRect().width() <= ui::px(4) + 0.01);
        const auto transposedHorizontal = ReadingPage::annotationUnderline({QRect(24, 483, 46, 69)}, false);
        QCOMPARE(shortVertical, QTransform(0, 1, 1, 0, 0, 0).map(transposedHorizontal));
        // A temporary candidate is separate from the draft and must not join it.
        ReadingPage page(nullptr, [] {});
        page.annotate({lines.first()}, {}, {lines[1]});
        QVERIFY(page.annotationContains(QPoint(160, 115)));
        QVERIFY(!page.annotationContains(QPoint(160, 133)));
        QVERIFY(!page.annotationContains(QPoint(160, 150)));
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts:freetype");
    QApplication app(argc, argv);
    if (QGuiApplication::platformName() != "minimal")
        return 2;
    app.setStyle("Fusion");
    auto &assets = test::readerAssets();
    if (!assets.available() || !ui::initializeInterfaceFonts(assets))
        return 2;
    for (const auto &path : bundledFontFiles(assets))
        if (QFontDatabase::addApplicationFont(path) < 0)
            return 2;
    ui::setDensity(300);
    app.setFont(ui::interfaceFont());
    ReaderUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "reader_ui.moc"
