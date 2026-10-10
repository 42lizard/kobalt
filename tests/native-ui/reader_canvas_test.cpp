#include "reader_canvas.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>

namespace {
class Surface : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.fillRect(QRect(750, 1050, 50, 50), Qt::black);
    }
};
class ResizeCreatesChildren : public QWidget {
public:
    using QWidget::QWidget;
    QWidget *prior = nullptr, *created = nullptr, *forced = nullptr, *forcedChild = nullptr;

protected:
    void resizeEvent(QResizeEvent *) override {
        if (created)
            return;
        auto *button = new QPushButton("A rendered row", this);
        button->setGeometry(10, 10, 220, 60);
        button->setStyleSheet("background:black;color:white;border:none;");
        created = button;
        if (prior)
            prior->setParent(created);
        forced = new QWidget(this);
        forced->setUpdatesEnabled(false);
        forcedChild = new QWidget(forced);
    }
};

// Exercise the reader's existing global-coordinate dismissal contract without
// bringing the renderer or Nickel services into this host-container test.
class PopupDismissal : public QObject {
public:
    explicit PopupDismissal(QWidget *popup) : popup(popup) {
        qApp->installEventFilter(this);
    }
    bool dismissed = false;

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::MouseButtonPress || !popup->isVisible())
            return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QRect bounds(popup->mapToGlobal(QPoint()), popup->size());
        if (bounds.contains(mouse->globalPosition().toPoint()))
            return false;
        dismissed = true;
        popup->hide();
        return true;
    }

private:
    QWidget *popup;
};

// Send through the actual top-level input path. QTest::mouseClick(child) would
// skip hit testing, and sending directly to the viewport would target the
// viewport even when the pointer lies over one of its child widgets.
void clickSurface(kobalt::test::ReaderCanvas &canvas, QPoint point) {
    const QPoint viewportPoint = canvas.surface()->mapTo(canvas.viewport(), point);
    QVERIFY(canvas.viewport()->rect().contains(viewportPoint));
    const QPoint windowPoint = canvas.viewport()->mapTo(&canvas, viewportPoint);
    QTest::mouseClick(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPoint);
}
}

class ReaderCanvasTest : public QObject {
    Q_OBJECT
private slots:
    void capturePreservesUpdateOwnership() {
        auto *surface = new Surface;
        auto *panel = new ResizeCreatesChildren(surface);
        panel->hide();
        panel->setGeometry(0, 0, 300, 200);
        auto *prior = new QWidget(panel);
        panel->prior = prior;
        prior->setAttribute(Qt::WA_UpdatesDisabled, true);
        auto *oldForced = new QWidget(surface);
        oldForced->setUpdatesEnabled(false);
        kobalt::test::ReaderCanvas canvas(surface, QSize(800, 1100));
        canvas.resize(400, 450);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(!panel->created);
        QVERIFY(!canvas.capture().isNull());
        QVERIFY(panel->created);
        QVERIFY(panel->updatesEnabled());
        QVERIFY(panel->created->updatesEnabled());
        QVERIFY(!prior->updatesEnabled());
        QVERIFY(!oldForced->updatesEnabled());
        QVERIFY(!panel->forced->updatesEnabled());
        QVERIFY(!panel->forcedChild->updatesEnabled());
        panel->show();
        const auto image = canvas.capture();
        QCOMPARE(image.pixelColor(20, 20), QColor(Qt::black));
        QVERIFY(!prior->updatesEnabled());
        QVERIFY(!oldForced->updatesEnabled());
        QVERIFY(!panel->forced->updatesEnabled());
        QVERIFY(!panel->forcedChild->updatesEnabled());
    }
    void scrolledInputAndCapture() {
        auto *surface = new Surface;
        auto *popup = new QWidget(surface);
        popup->setGeometry(160, 450, 300, 220);
        auto *choice = new QPushButton("Choose font", popup);
        choice->setGeometry(20, 25, 130, 60);
        QSignalSpy clicks(choice, &QPushButton::clicked);
        kobalt::test::ReaderCanvas canvas(surface, QSize(800, 1100));
        canvas.resize(400, 450);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.horizontalScrollBar()->setValue(150);
        canvas.verticalScrollBar()->setValue(350);
        QCOMPARE(canvas.horizontalScrollBar()->value(), 150);
        QCOMPARE(canvas.verticalScrollBar()->value(), 350);
        PopupDismissal dismissal(popup);

        clickSurface(canvas, choice->mapTo(surface, choice->rect().center()));
        QTRY_COMPARE(clicks.count(), 1);
        QVERIFY(popup->isVisible());
        QVERIFY(!dismissal.dismissed);

        const QPoint outside(470, 540);
        QVERIFY(!popup->geometry().contains(outside));
        clickSurface(canvas, outside);
        QTRY_VERIFY(dismissal.dismissed);
        QVERIFY(!popup->isVisible());
        QCOMPARE(clicks.count(), 1);

        const QImage scrolled = canvas.capture();
        QCOMPARE(scrolled.size(), QSize(800, 1100));
        QCOMPARE(scrolled.devicePixelRatio(), qreal(1));
        QCOMPARE(scrolled.pixelColor(775, 1075), QColor(Qt::black));
        QCOMPARE(scrolled.pixelColor(20, 20), QColor(Qt::white));
        canvas.horizontalScrollBar()->setValue(0);
        canvas.verticalScrollBar()->setValue(0);
        QCOMPARE(canvas.capture(), scrolled);
    }
};

QTEST_MAIN(ReaderCanvasTest)
#include "reader_canvas_test.moc"
