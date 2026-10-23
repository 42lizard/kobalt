#include "support/paint_regions.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/loading.h"
#include "interfaces/reader/ui/controls/sliders.h"
#include "interfaces/reader/ui/dialogs/modals.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/ui/style/styles.h"
#include "interfaces/reader/ui/style/theme.h"
#include "interfaces/reader/views/opening.h"
#include "interfaces/reader/views/reading_page.h"
#include "interfaces/reader/panels/bookmark_list.h"
#include "platforms/android/canvas.h"
#include "platforms/kobo/display/annotation_damage.h"
#include "platforms/kobo/display/panel_damage.h"
#include "support/host_presentation.h"
#include "support/reader_inputs.h"
#include "engine/crengine/reader_model.h"
#include <QApplication>
#include <QFontDatabase>
#include <QLocale>
#include <QDir>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
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
    void annotationModeNotice_data() { variants(); }
    void annotationModeNotice() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        ui::setDensity(ppi);
        QWidget host;
        host.setProperty("reader_dark", dark);
        ui::readerStyle(&host, ui::styles::reader());
        host.resize(ui::px(1072), ui::px(1448));
        ui::AnnotationModeNotice notice(&host);
        notice.centerIn(host.size());
        host.show();
        notice.show();
        notice.setModeEnabled(true);
        QCoreApplication::processEvents();
        const auto enabled = notice.grab().toImage();
        notice.setModeEnabled(false);
        const auto disabled = notice.grab().toImage();
        const QRect icon(ui::metrics::lightSwipe::inset,
                         (notice.height() - ui::metrics::lightSwipe::iconWidth) / 2,
                         ui::metrics::lightSwipe::iconWidth, ui::metrics::lightSwipe::iconWidth);
        QVERIFY(enabled.copy(icon) != disabled.copy(icon));
        QCOMPARE(notice.size(), QSize(ui::metrics::lightSwipe::width, ui::metrics::lightSwipe::height));
        QVERIFY(notice.testAttribute(Qt::WA_TransparentForMouseEvents));
        const auto captures = qEnvironmentVariable("KOBALT_CAPTURE_MODALS");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            const auto suffix = QString("-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi);
            QVERIFY(enabled.save(captures + "/mode-on" + suffix));
            QVERIFY(disabled.save(captures + "/mode-off" + suffix));
        }
    }
    void connectedModalFooter_data() { variants(); }
    void connectedModalFooter() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        ui::setDensity(ppi);
        QWidget host;
        host.setProperty("reader_dark", dark);
        auto colors = host.palette();
        const QColor paper = dark ? Qt::black : Qt::white;
        const QColor ink = dark ? Qt::white : Qt::black;
        colors.setColor(QPalette::Window, paper);
        colors.setColor(QPalette::WindowText, ink);
        host.setPalette(colors);
        ui::readerStyle(&host, ui::styles::reader());
        host.resize(ui::px(1072), ui::px(1448));
        host.show();
        ui::ModalOverlay reference(&host);
        reference.setDimmed(true);
        reference.show();
        QCoreApplication::processEvents();
        const auto backdrop = reference.grab().toImage();
        reference.hide();
        ui::ChoiceModal choice(&host, {"The saved passage will be removed from this book.", "Delete this highlight?", {}},
                                {"Cancel", "modalCancel"}, {"Delete", "modalDelete"});
        choice.show();
        QCoreApplication::processEvents();
        auto *card = choice.card();
        auto *cancel = choice.secondaryButton();
        auto *remove = choice.primaryButton();
        auto *heading = card->findChild<QLabel *>("rrModalTitle");
        QVERIFY(heading->height() >= QFontMetrics(heading->font()).height() + 2 * ui::metrics::notice::cardMargins.top());
        QVERIFY(choice.descriptionLabel()->contentsMargins().left() >= ui::metrics::notice::cardMargins.left());
        QCOMPARE(cancel->y(), remove->y());
        QCOMPARE(cancel->geometry().right() + 1, remove->x());
        QCOMPARE(cancel->x(), 2);
        QCOMPARE(remove->geometry().right(), card->width() - 3);
        QCOMPARE(remove->geometry().bottom(), card->height() - 3);
        const auto frame = card->grab().toImage();
        QCOMPARE(frame.pixelColor(remove->geometry().center().x(), remove->y()), ui::dividerColor(card));
        QCOMPARE(frame.pixelColor(remove->x(), remove->y() + ui::px(10)), ui::dividerColor(card));
        QCOMPARE(frame.pixelColor(remove->x() + ui::px(10), remove->y() + ui::px(10)), paper);
        const auto actual = choice.grab().toImage();
        int stripes = 0;
        for (int y = 0; y < ui::px(100); ++y) {
            QCOMPARE(actual.pixelColor(0, y), backdrop.pixelColor(0, y));
            stripes += actual.pixelColor(0, y) != paper;
        }
        QVERIFY(stripes > 0);
        const auto captures = qEnvironmentVariable("KOBALT_CAPTURE_MODALS");
        if (!captures.isEmpty()) {
            QVERIFY(QDir().mkpath(captures));
            QVERIFY(choice.grab().save(captures + QString("/connected-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi)));
        }
        remove->setText("Use this reading font as the default for every new book");
        card->fitToWidth(ui::px(400));
        choice.centerCard();
        QCoreApplication::processEvents();
        QVERIFY(remove->y() < cancel->y());
        QCOMPARE(remove->geometry().bottom() + 1, cancel->y());
        QCOMPARE(remove->x(), cancel->x());
        QCOMPARE(remove->width(), cancel->width());
        const auto labelBounds = QFontMetrics(remove->font()).boundingRect(
            QRect(0, 0, remove->width() - 2 * int(ui::metrics::control::outlinedHorizontalPadding + ui::metrics::control::border), 10000),
            Qt::TextWordWrap, remove->text());
        QVERIFY(remove->height() >= labelBounds.height() + 2 * int(ui::metrics::control::outlinedVerticalPadding + ui::metrics::control::border));
        if (!captures.isEmpty())
            QVERIFY(choice.grab().save(captures + QString("/stacked-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi)));
        remove->hide();
        card->fitToWidth(ui::px(760));
        QCoreApplication::processEvents();
        QCOMPARE(cancel->width(), card->width() - 4);
        remove->show();
        remove->setText("Delete");
        card->fitToWidth(ui::px(760));
        QCoreApplication::processEvents();
        QCOMPARE(cancel->y(), remove->y());
        choice.hide();
        ui::NoticeModal notice(&host, {{}, "Formatting warning", {}}, {"OK", "modalOk"});
        notice.show();
        QCoreApplication::processEvents();
        QCOMPARE(notice.primaryButton()->width(), notice.card()->width() - 4);
        QCOMPARE(notice.primaryButton()->geometry().bottom(), notice.card()->height() - 3);
        QCOMPARE(notice.grab().toImage().pixelColor(0, 0), backdrop.pixelColor(0, 0));
    }
    void marksSortingAndAnnotationRows_data() { variants(); }
    void marksSortingAndAnnotationRows() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        ui::setDensity(ppi);
        QWidget owner;
        owner.setProperty("reader_dark", dark);
        auto colors = owner.palette();
        colors.setColor(QPalette::Window, dark ? Qt::black : Qt::white);
        colors.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        colors.setColor(QPalette::ButtonText, dark ? Qt::white : Qt::black);
        owner.setPalette(colors);
        ui::readerStyle(&owner, ui::styles::reader());
        owner.resize(ui::px(1072), ui::px(1448));
        BookmarkList list(&owner);
        list.setExcerptFont(QStringLiteral("Libron"));
        list.setGeometry(owner.rect());
        const auto early = QDateTime::fromString("2026-10-20T09:12:00Z", Qt::ISODate);
        const auto late = early.addDays(1);
        BookmarkRow a{"first", "Chapter One", QStringLiteral("A lengthy quotation with enough words to wrap across several lines. ").repeated(5), late, 2};
        a.sourceOrder = 0;
        a.style = "dotted";
        a.color = "blue";
        const auto sampleFont = ui::interfaceFont(ui::px(32));
        QImage wrapped(300, 8 * ui::px(44), QImage::Format_RGB32);
        wrapped.fill(Qt::white);
        {
            QPainter painter(&wrapped);
            painter.setFont(sampleFont);
            painter.setPen(Qt::black);
            QVector<QRect> lines;
            ui::drawWrappedInterfaceText(painter, wrapped.rect(), a.excerpt, Qt::AlignLeft | Qt::AlignTop,
                                          2, ui::px(44), &lines);
            QCOMPARE(lines.size(), 2);
            QCOMPARE(lines[1].top() - lines[0].top(), int(ui::px(44)));
        }
        for (int y = 2 * ui::px(44) + 2; y < wrapped.height(); ++y)
            for (int x = 0; x < wrapped.width(); ++x)
                QCOMPARE(wrapped.pixelColor(x, y), QColor(Qt::white));
        BookmarkRow b{"second", "Chapter One", "Second passage", early, 2};
        b.sourceOrder = 1;
        b.style = "underline";
        b.color = "green";
        BookmarkRow c{"third", "Chapter Two", "Later in the book", late.addSecs(60), 8};
        QString selected, changed;
        list.selected = [&](const QString &id) { selected = id; };
        list.sortRequested = [&](const QString &value) { changed = value; };
        owner.show();
        list.showAnnotations({c, b, a});
        QCoreApplication::processEvents();
        const auto row = [&]() { return static_cast<ui::ListRow *>(list.findChild<QWidget *>("rrAnnotation0")); };
        QVERIFY(row());
        row()->click();
        QCOMPARE(selected, QString("first"));
        QCOMPARE(row()->detailLabel()->text(), a.excerpt);
        QVERIFY(row()->titleLabel()->font().pixelSize() < row()->detailLabel()->font().pixelSize());
        QVERIFY(row()->detailLabel()->y() - row()->titleLabel()->geometry().bottom() >= ui::px(12));
        QVERIFY(row()->detailLabel()->wordWrap());
        QVERIFY(row()->detailLabel()->height() > 2 * QFontMetrics(row()->detailLabel()->font()).height());
        QVERIFY(!row()->captionLabel()->text().contains("Dotted underline"));
        QVERIFY(row()->captionLabel()->text().contains(QLocale().toString(late.toLocalTime(), "HH:mm")));
        const int annotationHeight = row()->height();
        QCOMPARE(list.findChild<QPushButton *>("rrMarksSort")->y(), list.findChild<QPushButton *>("rrAnnotationsTab")->y());
        auto *sortControl = list.findChild<QPushButton *>("rrMarksSort");
        const auto sortImage = sortControl->grab().toImage();
        const auto iconArea = QRect(sortImage.rect().center() - QPoint(ui::px(20), ui::px(20)),
                                    QSize(ui::px(40), ui::px(40)));
        int iconPixels = 0;
        const auto ink = colors.color(QPalette::WindowText);
        for (int y = iconArea.top(); y <= iconArea.bottom(); ++y)
            for (int x = iconArea.left(); x <= iconArea.right(); ++x)
                iconPixels += sortImage.pixelColor(x, y) == ink;
        QVERIFY(iconPixels > 10);
        QVERIFY(list.findChild<QPushButton *>("rrMarksSort")->x() >= list.findChild<QPushButton *>("rrBookmarksTab")->geometry().right());
        QVector<QImage> appearances;
        for (const auto *style : {"shaded", "underline", "dotted"}) {
            auto item = a;
            item.style = style;
            list.showAnnotations({item});
            QCoreApplication::processEvents();
            appearances.append(row()->detailLabel()->grab().toImage());
        }
        QVERIFY(appearances[0] != appearances[1]);
        QVERIFY(appearances[1] != appearances[2]);
        QVERIFY(appearances[0] != appearances[2]);
        list.showAnnotations({c, b, a});
        auto *shortRow = list.findChild<QWidget *>("rrAnnotation1");
        QVERIFY(shortRow && shortRow->height() < annotationHeight);
        const auto shortSample = static_cast<ui::ListRow *>(shortRow)->detailLabel()->grab().toImage();
        bool hasGreenUnderline = false;
        for (int y = 0; y < shortSample.height() && !hasGreenUnderline; ++y)
            for (int x = 0; x < shortSample.width(); ++x)
                hasGreenUnderline = hasGreenUnderline || shortSample.pixelColor(x, y) == QColor("#b8d2aa");
        QVERIFY(hasGreenUnderline);
        const auto captureRoot = qEnvironmentVariable("KOBALT_CAPTURE_MARKS");
        if (!captureRoot.isEmpty()) {
            QVERIFY(QDir().mkpath(captureRoot));
            QVERIFY(owner.grab().save(captureRoot + QString("/annotations-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi)));
        }
        list.findChild<QPushButton *>("rrMarksSort")->click();
        QCoreApplication::processEvents();
        if (!captureRoot.isEmpty())
            QVERIFY(owner.grab().save(captureRoot + QString("/sort-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi)));
        QCOMPARE(list.findChild<QWidget *>("rrModalCard")->palette().color(QPalette::Window),
                 colors.color(QPalette::Window));
        auto *newestOption = list.findChild<QAbstractButton *>("rrMarksSort_newest");
        QCOMPARE(newestOption->palette().color(QPalette::Window), colors.color(QPalette::Window));
        QCOMPARE(newestOption->palette().color(QPalette::WindowText), colors.color(QPalette::WindowText));
        QVERIFY(!newestOption->accessibleDescription().isEmpty());
        list.findChild<QAbstractButton *>("rrMarksSort_newest")->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(changed, QString("newest"));
        row()->click();
        QCOMPARE(selected, QString("third"));
        list.findChild<QPushButton *>("rrMarksSort")->click();
        QCoreApplication::processEvents();
        list.findChild<QAbstractButton *>("rrMarksSort_oldest")->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        row()->click();
        QCOMPARE(selected, QString("second"));
        list.showAnnotations({a, c, b});
        QCOMPARE(list.sortOrder(), QString("oldest"));
        row()->click();
        QCOMPARE(selected, QString("second"));
        list.setSortOrder("position");
        list.showBookmarks({c, b, a});
        auto *bookmark = static_cast<ui::ListRow *>(list.findChild<QWidget *>("rrBookmark0"));
        QVERIFY(bookmark);
        bookmark->click();
        QCOMPARE(selected, QString("first"));
        QVERIFY(bookmark->detailLabel()->wordWrap());
        QCOMPARE(bookmark->height(), annotationHeight);
        QCOMPARE(bookmark->titleLabel()->font().pixelSize(), int(ui::px(24)));
        QVERIFY(bookmark->detailLabel()->y() - bookmark->titleLabel()->geometry().bottom() >= ui::px(12));
        if (!captureRoot.isEmpty())
            QVERIFY(owner.grab().save(captureRoot + QString("/bookmarks-%1-%2.png").arg(dark ? "dark" : "light").arg(ppi)));
        QVERIFY(bookmark->captionLabel()->text().contains(QLocale().toString(late.toLocalTime(), "HH:mm")));
        QVector<BookmarkRow> many;
        for (int i = 0; i < 24; ++i) {
            auto item = i % 2 ? a : b;
            item.position = QString::number(i);
            item.page = i;
            many.append(item);
        }
        list.showAnnotations(many);
        QSet<QString> visited;
        for (int turn = 0; turn < 24 && visited.size() < many.size(); ++turn) {
            for (auto *button : list.findChildren<QPushButton *>()) {
                const auto name = button->objectName();
                if (!button->isVisible() || !name.startsWith("rrAnnotation") ||
                    name.mid(QStringLiteral("rrAnnotation").size()).toInt() < 0 ||
                    name.contains("Remove") || name == "rrAnnotationsTab")
                    continue;
                button->click();
                QVERIFY(!visited.contains(selected));
                visited.insert(selected);
            }
            if (visited.size() < many.size())
                list.findChild<QPushButton *>("rrBookmarkListNext")->click();
        }
        QCOMPARE(visited.size(), many.size());
    }
    void hidingSelectionMenuKeepsDamageCompact_data() {
        QTest::addColumn<bool>("barrier");
        QTest::newRow("blocking-with-grip-holes") << true;
        QTest::newRow("nonblocking-card-only") << false;
    }
    void hidingSelectionMenuKeepsDamageCompact() {
        QFETCH(bool, barrier);
        // Use a native backing store. A graphics proxy redraws its cached surface
        // and conceals the fragmented exposure caused by circular input holes.
        QWidget owner;
        owner.resize(1264, 1680);
        ReadingPage paper(&owner, [] {});
        paper.setGeometry(owner.rect());
        paper.image = QImage(owner.size(), QImage::Format_RGB32);
        paper.image.fill(Qt::white);
        paper.setAnnotationInputEnabled(true);
        AnnotationGeometry geometry;
        geometry.rectangles = {QRect(200, 500, 120, 30), QRect(900, 1150, 120, 30)};
        geometry.start = {QPoint(200, 500), QPoint(200, 530), QPoint(-1, 0), true};
        geometry.end = {QPoint(1020, 1150), QPoint(1020, 1180), QPoint(1, 0), true};
        paper.setAnnotationHandles(geometry);
        ui::ModalOverlay overlay(&owner);
        overlay.setInputBarrier(barrier);
        overlay.card()->resize(700, 300);
        overlay.anchorTo(QRect(550, 650, 100, 50));
        overlay.setInputExclusions(paper.annotationHandleHitRegion());
        PaintRegions paints;
        paper.installEventFilter(&paints);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QCoreApplication::processEvents();
        QRegion shownMask;
        for (int repeat = 0; repeat < 2; ++repeat) {
            overlay.show();
            QCoreApplication::processEvents();
            if (barrier)
                QVERIFY(overlay.mask().rectCount() > 20);
            if (repeat == 0)
                shownMask = overlay.mask();
            else
                QCOMPARE(overlay.mask(), shownMask);
            for (const bool start : {true, false}) {
                const auto grip = paper.annotationHandleRect(start).center();
                QCOMPARE(owner.childAt(grip), &paper);
            }
            QVERIFY(overlay.mask().contains(overlay.card()->geometry().center()));
            paints.count = 0;
            paints.painted = {};
            overlay.hide();
            QCoreApplication::processEvents();
            QVERIFY(paints.count > 0);
            const auto damage = panelDamage(&owner, &paper, paints.painted);
            QVERIFY2(damage.size() <= 4, qPrintable(QString("Menu dismissal produced %1 screen-update rectangles")
                                                   .arg(damage.size())));
            if (!barrier)
                QVERIFY(shownMask.boundingRect().contains(paints.painted.boundingRect()));
        }
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
        const int baseline = passage.bottom() + page.topHeight();
        const QRect outer = passage.translated(0, page.topHeight()).adjusted(-2, -2, 2, 2);
        const QRect inner = passage.translated(0, page.topHeight()).adjusted(2, 2, -2, -2);
        for (int row = 0; row < draft.height(); ++row)
            for (int col = 0; col < draft.width(); ++col)
                if (!outer.contains(col, row) || inner.contains(col, row))
                    QCOMPARE(draft.pixelColor(col, row), plain.pixelColor(col, row));
        for (const QPoint &edge : {QPoint(x, y), QPoint(x, baseline),
                                    QPoint(passage.left(), interior.y()), QPoint(passage.right(), interior.y())})
            QVERIFY(draft.pixelColor(edge) != plain.pixelColor(edge));
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
        page.setSavedHighlights({{QVector<QRect>{passage, passage}, "yellow"}});
        const QImage yellow = page.snapshot();
        QCOMPARE(yellow.pixelColor(interior), plain.pixelColor(interior));
        QVERIFY(yellow.pixelColor(x + 10, y + 10) != gray);
        QVERIFY(page.property("reader_colour_regions").toBool());
        page.setSavedHighlights({{QVector<QRect>{passage}, "blue"}});
        const QImage blue = page.snapshot();
        QCOMPARE(blue.pixelColor(interior), plain.pixelColor(interior));
        QVERIFY(blue.pixelColor(x + 10, y + 10) != yellow.pixelColor(x + 10, y + 10));
        page.setSavedHighlights({{QVector<QRect>{passage}, "gray", "underline"}});
        const auto underlined = page.snapshot();
        QCOMPARE(underlined.pixelColor(interior), plain.pixelColor(interior));
        QCOMPARE(underlined.pixelColor(x + 10, y + 10), plain.pixelColor(x + 10, y + 10));
        QVERIFY(underlined != plain && underlined != saved);
        page.setSavedHighlights({{QVector<QRect>{passage}, "gray", "dotted"}});
        const auto dotted = page.snapshot();
        QCOMPARE(dotted.pixelColor(interior), plain.pixelColor(interior));
        QVERIFY(dotted != plain && dotted != underlined && dotted != saved);
        bool hasInk = false, hasGap = false;
        for (int at = passage.left(); at <= passage.right(); ++at) {
            const QPoint point(at, baseline);
            hasInk |= dotted.pixelColor(point) != plain.pixelColor(point);
            hasGap |= dotted.pixelColor(point) == plain.pixelColor(point);
        }
        QVERIFY(hasInk && hasGap);
        page.setSavedHighlights({{QVector<QRect>{passage}, "blue", "underline"}});
        const auto blueUnderline = page.snapshot();
        QVERIFY(blueUnderline != underlined);
        const QColor lineColor = blueUnderline.pixelColor(passage.left() + 10, baseline);
        QVERIFY(lineColor.blue() > lineColor.red());
        page.setSavedHighlights({{QVector<QRect>{passage}, "blue", "dotted"}});
        QVERIFY(page.snapshot() != dotted);
        page.setSavedHighlights({{QVector<QRect>{passage}, "yellow"},
                                  {QVector<QRect>{passage}, "gray"}});
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
        const QVector<HighlightGeometry> blue{{lines, "blue"}};
        const QVector<HighlightGeometry> pink{{lines.mid(0, 3), "pink"}, {lines.mid(3), "green"}};
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
                                                         page.setSavedHighlights(blue);
                                                     },
                                                     [&] {
                                                         page.setSavedHighlights(pink);
                                                     },
                                                     [&] {
                                                         page.annotate(lines.mid(1, 4), lines);
                                                     },
                                                     [&] {
                                                         page.annotate({}, lines);
                                                     },
                                                     [&] {
                                                         page.setSavedHighlights({});
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
        page.setSavedHighlights({});
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
        const QVector<HighlightGeometry> blue{{lines, "blue"}};
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
    void annotationHandlesDragAndCancel() {
        ReadingPage page(nullptr, [] {});
        page.resize(600, 800);
        page.show();
        page.image = QImage(600, 700, QImage::Format_RGB32);
        page.image.fill(Qt::white);
        page.refreshContent();
        page.setAnnotationInputEnabled(true);
        AnnotationGeometry geometry;
        geometry.rectangles = {{100, 100, 200, 40}};
        geometry.start = {{100, 100}, {100, 140}, {-1, 0}, true};
        geometry.end = {{300, 100}, {300, 140}, {1, 0}, true};
        page.annotate(geometry.rectangles, {});
        page.setAnnotationHandles(geometry);
        const auto first = page.annotationHandleRect(true), last = page.annotationHandleRect(false);
        QVERIFY(!first.isEmpty() && !last.isEmpty());
        QVERIFY(first.bottom() <= geometry.rectangles.first().top() + page.topHeight());
        QVERIFY(last.top() >= geometry.rectangles.last().bottom() + page.topHeight());
        int boundary = -2, taps = 0, swipes = 0, finishes = 0;
        bool cancelled = false;
        QPoint moved;
        page.tapped = [&](QPoint) { ++taps; };
        page.swiped = [&](int) { ++swipes; };
        page.annotationDragStarted = [&](QPoint, int value) { boundary = value; return true; };
        page.annotationDragMoved = [&](QPoint point) { moved = point; };
        page.annotationDragFinished = [&](bool value) { cancelled = value; ++finishes; };
        QTest::mousePress(&page, Qt::LeftButton, Qt::NoModifier, first.center());
        QCOMPARE(boundary, -2);
        QTest::mouseMove(&page, first.center() + QPoint(150, 80));
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, first.center() + QPoint(150, 80));
        QCOMPARE(boundary, 0);
        QCOMPARE(finishes, 1);
        QVERIFY(!cancelled);
        QVERIFY(!moved.isNull());
        QCOMPARE(taps, 0);
        QCOMPARE(swipes, 0);
        QTest::mousePress(&page, Qt::LeftButton, Qt::NoModifier, last.center());
        QCOMPARE(boundary, 0);
        QTest::mouseMove(&page, last.center() + QPoint(80, 40));
        QCOMPARE(boundary, 1);
        page.cancelHold();
        QCOMPARE(finishes, 2);
        QVERIFY(cancelled);
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, last.center());
        QCOMPARE(finishes, 2);
        QCOMPARE(taps, 0);
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto *device = QTest::createTouchDevice();
        auto touch = QTest::touchEvent(&page, device, false);
        touch.press(0, last.center(), &page).commit();
        QCOMPARE(boundary, 1);
        touch.move(0, last.center() + QPoint(80, 40), &page).commit();
        touch.release(0, last.center() + QPoint(80, 40), &page).commit();
        QCOMPARE(finishes, 3);
        QVERIFY(!cancelled);
        QCOMPARE(taps + swipes, 0);
        touch.press(0, first.center(), &page).commit();
        touch.move(0, first.center() + QPoint(80, 40), &page).commit();
        QCOMPARE(boundary, 0);
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QApplication::sendEvent(&page, &cancel);
        QCOMPARE(finishes, 4);
        QVERIFY(cancelled);
        touch.release(0, first.center(), &page).commit();
        QCOMPARE(finishes, 4);
        const int previousFinishes = finishes;
        QTest::mouseClick(&page, Qt::LeftButton, Qt::NoModifier,
                          QPoint(geometry.rectangles.last().right() - 2, geometry.rectangles.last().bottom() - 2) +
                              QPoint(0, page.topHeight()));
        QCOMPARE(taps, 1);
        QCOMPARE(finishes, previousFinishes);
        geometry.start.visible = false;
        page.setAnnotationHandles(geometry);
        QVERIFY(page.annotationHandleRect(true).isEmpty());
        QVERIFY(!page.annotationHandleRect(false).isEmpty());
        page.annotate({}, {});
        QVERIFY(page.annotationHandleRect(false).isEmpty());
        // A normal text swipe keeps page navigation; only a hold arms selection.
        const QPoint textPoint(380, 400);
        boundary = -2;
        const int previousSwipes = swipes;
        QTest::mousePress(&page, Qt::LeftButton, Qt::NoModifier, textPoint);
        QTest::mouseMove(&page, textPoint - QPoint(180, 0));
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, textPoint - QPoint(180, 0));
        QCOMPARE(swipes, previousSwipes + 1);
        QCOMPARE(boundary, -2);
        const int beforeHold = finishes;
        QTest::mousePress(&page, Qt::LeftButton, Qt::NoModifier, textPoint);
        QTRY_COMPARE(boundary, -1);
        QTest::mouseMove(&page, textPoint - QPoint(180, 0));
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, textPoint - QPoint(180, 0));
        QCOMPARE(finishes, beforeHold + 1);
        QCOMPARE(swipes, previousSwipes + 1);
        QVERIFY(!cancelled);
        boundary = -2;
        touch.press(0, textPoint, &page).commit();
        QTRY_COMPARE(boundary, -1);
        touch.move(0, textPoint + QPoint(80, 0), &page).commit();
        touch.release(0, textPoint + QPoint(80, 0), &page).commit();
        QCOMPARE(finishes, beforeHold + 2);
        QCOMPARE(swipes, previousSwipes + 1);
        page.setAnnotationInputEnabled(false);
        page.setAnnotationHandles(geometry);
        QVERIFY(page.annotationHandleRect(false).isEmpty());
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
        const auto shortVertical = ReadingPage::annotationContour({QRect(483, 24, 69, 46)}, true);
        QCOMPARE(shortVertical.boundingRect(), QRect(483, 24, 69, 46));
        const auto transposedHorizontal = ReadingPage::annotationContour({QRect(24, 483, 46, 69)}, false);
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
