#include "platform/nickel/qt5/touch_input.h"
#include "interfaces/reader/ui/controls/paging.h"
#include <QApplication>
#include <QCheckBox>
#include <QEventPoint>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTest>
#include <QTouchEvent>
#include <QWindow>

namespace {
class TouchViewport : public QWidget {
    Q_OBJECT
};
class Fixture {
public:
    QWidget window;
    QScrollArea area{&window};
    QWidget *body = new QWidget;
    QPushButton *check = new QPushButton("A mouse-only control", body);
    QObject inputOwner;
    const QPointingDevice *device = QTest::createTouchDevice();
    explicit Fixture(bool customViewport = false) {
        if (customViewport)
            area.setViewport(new TouchViewport);
        window.setObjectName("rrTouchInputTest");
        window.resize(220, 300);
        area.setGeometry(10, 10, 200, 280);
        body->resize(180, 1000);
        check->setGeometry(10, 40, 160, 140);
        check->setCheckable(true);
        area.setWidget(body);
        area.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
        kobalt::nickel4::installTouchInput(&inputOwner);
        window.show();
        QApplication::processEvents();
    }
    QPoint center() const {
        return check->mapToGlobal(check->rect().center());
    }
    void touch(QEvent::Type type, const QPoint &point, int count = 1) {
        QList<QEventPoint> points;
        const auto state = type == QEvent::TouchBegin ? QEventPoint::State::Pressed
                           : type == QEvent::TouchEnd ? QEventPoint::State::Released
                                                      : QEventPoint::State::Updated;
        for (int id = 0; id < count; ++id)
            points.append(
                QEventPoint(id, state, window.mapFromGlobal(point + QPoint(id * 20, 0)), point + QPoint(id * 20, 0)));
        QTouchEvent event(type, device, Qt::NoModifier, points);
        QApplication::sendEvent(window.windowHandle(), &event);
    }
};
}
class TouchInputTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");
        QApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, false);
    }
    void scrollChildTapActivatesOnce() {
        Fixture f;
        QVERIFY(f.area.viewport()->testAttribute(Qt::WA_AcceptTouchEvents));
        QVERIFY(!f.check->testAttribute(Qt::WA_AcceptTouchEvents));
        QCOMPARE(QApplication::widgetAt(f.center()), f.check);
        QSignalSpy clicked(f.check, &QPushButton::clicked);
        f.touch(QEvent::TouchBegin, f.center());
        QVERIFY(f.check->isDown());
        f.touch(QEvent::TouchEnd, f.center());
        QCOMPARE(clicked.size(), 1);
        QVERIFY(f.check->isChecked());
        QVERIFY(!f.check->isDown());
        QVERIFY(f.area.viewport()->testAttribute(Qt::WA_AcceptTouchEvents));
    }
    void checkboxTapPersistsOnce() {
        Fixture f;
        QCheckBox checkbox("A persisted option", f.body);
        checkbox.setGeometry(10, 210, 160, 40);
        checkbox.show();
        const QPoint point = checkbox.mapToGlobal(QPoint(10, 20));
        QCOMPARE(QApplication::widgetAt(point), &checkbox);
        QSignalSpy clicked(&checkbox, &QCheckBox::clicked);
        f.touch(QEvent::TouchBegin, point);
        f.touch(QEvent::TouchEnd, point);
        QCOMPARE(clicked.size(), 1);
        QVERIFY(checkbox.isChecked());
    }
    void movedGestureDoesNotActivate_data() {
        QTest::addColumn<bool>("update");
        QTest::newRow("update-and-end") << true;
        QTest::newRow("end-without-update") << false;
    }
    void movedGestureDoesNotActivate() {
        QFETCH(bool, update);
        Fixture f;
        QSignalSpy clicked(f.check, &QPushButton::clicked);
        const QPoint begin = f.center();
        const QPoint end = begin + QPoint(0, QApplication::startDragDistance() + 5);
        QVERIFY(f.check->rect().contains(f.check->mapFromGlobal(end)));
        f.touch(QEvent::TouchBegin, begin);
        QVERIFY(f.check->isDown());
        if (update)
            f.touch(QEvent::TouchUpdate, end);
        f.touch(QEvent::TouchEnd, end);
        QCOMPARE(clicked.size(), 0);
        QVERIFY(!f.check->isChecked());
        QVERIFY(!f.check->isDown());
    }
    void cancellationDoesNotActivate_data() {
        QTest::addColumn<bool>("multitouch");
        QTest::newRow("cancel") << false;
        QTest::newRow("second-finger") << true;
    }
    void cancellationDoesNotActivate() {
        QFETCH(bool, multitouch);
        Fixture f;
        QSignalSpy clicked(f.check, &QPushButton::clicked);
        f.touch(QEvent::TouchBegin, f.center());
        QVERIFY(f.check->isDown());
        f.touch(multitouch ? QEvent::TouchUpdate : QEvent::TouchCancel, f.center(), multitouch ? 2 : 1);
        f.touch(QEvent::TouchEnd, f.center());
        QCOMPARE(clicked.size(), 0);
        QVERIFY(!f.check->isDown());
    }
    void intentionalTouchOwnerIsPreserved_data() {
        QTest::addColumn<int>("ownerKind");
        QTest::newRow("control") << 0;
        QTest::newRow("content-owner") << 1;
        QTest::newRow("custom-viewport") << 2;
    }
    void intentionalTouchOwnerIsPreserved() {
        QFETCH(int, ownerKind);
        Fixture f(ownerKind == 2);
        QWidget *owner = ownerKind == 2 ? f.area.viewport() : ownerKind == 1 ? f.body : f.check;
        owner->setAttribute(Qt::WA_AcceptTouchEvents);
        QSignalSpy clicked(f.check, &QPushButton::clicked);
        f.touch(QEvent::TouchBegin, f.center());
        f.touch(QEvent::TouchEnd, f.center());
        QCOMPARE(clicked.size(), 0);
        QVERIFY(!f.check->isDown());
        QVERIFY(owner->testAttribute(Qt::WA_AcceptTouchEvents));
    }
    void nativeMouseDoesNotClickTwice() {
        Fixture f;
        QSignalSpy clicked(f.check, &QPushButton::clicked);
        QTest::mouseClick(f.check, Qt::LeftButton);
        QCOMPARE(clicked.size(), 1);
        f.touch(QEvent::TouchBegin, f.center());
        f.touch(QEvent::TouchEnd, f.center());
        QCOMPARE(clicked.size(), 1);
    }
    void settingsPagerTapActivatesOnce() {
        QWidget window;
        window.setObjectName("rrSettingsPagerTest");
        window.resize(220, 300);
        kobalt::ui::PagerBar pager(&window, "rrSettingsOptions");
        pager.resize(220, pager.height());
        pager.setPage(0, 3);
        connect(pager.next, &QPushButton::clicked, &pager, [&] {
            pager.setPage(pager.turnTarget(1), 3);
        });
        connect(pager.previous, &QPushButton::clicked, &pager, [&] {
            pager.setPage(pager.turnTarget(-1), 3);
        });
        QObject inputOwner;
        kobalt::nickel4::installTouchInput(&inputOwner);
        window.show();
        QApplication::processEvents();
        auto *device = QTest::createTouchDevice();
        QSignalSpy clicked(pager.next, &QPushButton::clicked);
        const auto tap = [&](QWidget *button) {
            const auto point = button->mapTo(&window, button->rect().center());
            QCOMPARE(QApplication::widgetAt(window.mapToGlobal(point)), button);
            for (auto type : {QEvent::TouchBegin, QEvent::TouchEnd}) {
                const auto state =
                    type == QEvent::TouchBegin ? QEventPoint::State::Pressed : QEventPoint::State::Released;
                QTouchEvent event(type, device, Qt::NoModifier,
                                  {QEventPoint(0, state, point, window.mapToGlobal(point))});
                QApplication::sendEvent(window.windowHandle(), &event);
            }
            QApplication::processEvents();
        };
        tap(pager.next);
        QCOMPARE(clicked.size(), 1);
        QCOMPARE(pager.turnTarget(0), 1);
        tap(pager.next);
        QCOMPARE(clicked.size(), 2);
        QVERIFY(!pager.next->isEnabled());
        tap(pager.next);
        QCOMPARE(clicked.size(), 2);
        tap(pager.previous);
        QCOMPARE(pager.turnTarget(0), 1);
    }
    void scrollBarDragRemainsAvailable() {
        Fixture f;
        auto *bar = f.area.verticalScrollBar();
        QVERIFY(bar->isVisible());
        bar->setValue(100);
        QStyleOptionSlider option;
        option.initFrom(bar);
        option.orientation = Qt::Vertical;
        option.minimum = bar->minimum();
        option.maximum = bar->maximum();
        option.sliderPosition = option.sliderValue = bar->value();
        option.pageStep = bar->pageStep();
        option.upsideDown = false;
        const QRect handle =
            bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, bar);
        const QPoint begin = bar->mapToGlobal(handle.center());
        QCOMPARE(QApplication::widgetAt(begin), bar);
        f.touch(QEvent::TouchBegin, begin);
        f.touch(QEvent::TouchUpdate, begin + QPoint(0, 50));
        f.touch(QEvent::TouchEnd, begin + QPoint(0, 50));
        QVERIFY(bar->value() > 100);
    }
};
QTEST_MAIN(TouchInputTest)
#include "touch_input_test.moc"
