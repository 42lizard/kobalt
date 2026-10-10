#include "platform/nickel/common/window_presentation.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPointer>
#include <QTest>
#include <QWidget>

class MainWindowView : public QWidget {
    Q_OBJECT
};

class WindowPresentationTest : public QObject {
    Q_OBJECT
private slots:
    void pausePreservesReaderDescendantsAndPreviouslyDisabledWindows() {
        QWidget reader, background, hidden, disabled;
        QWidget child(&reader);
        QWidget childWindow(&reader, Qt::Window);
        reader.show();
        childWindow.show();
        background.show();
        disabled.show();
        disabled.setUpdatesEnabled(false);
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        presentation->pauseBackground();
        QVERIFY(reader.updatesEnabled());
        QVERIFY(child.updatesEnabled());
        // A top-level QWidget has its own backing store, even with a parent.
        QVERIFY(!childWindow.updatesEnabled());
        QVERIFY(!background.updatesEnabled());
        QVERIFY(hidden.updatesEnabled());
        QVERIFY(!disabled.updatesEnabled());
        presentation->pauseBackground();
        presentation->resumeBackground();
        QVERIFY(background.updatesEnabled());
        QVERIFY(childWindow.updatesEnabled());
        QVERIFY(!disabled.updatesEnabled());
        presentation->resumeBackground();
        QVERIFY(!disabled.updatesEnabled());
    }

    void wakePausesLibraryButKeepsNativeDialogsResponsive() {
        QWidget reader, pin, usb;
        MainWindowView library;
        reader.show();
        library.show();
        pin.show();
        usb.show();
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        presentation->pauseBackground(true);
        QVERIFY(!library.updatesEnabled());
        QVERIFY(pin.updatesEnabled());
        QVERIFY(usb.updatesEnabled());
        QVERIFY(reader.updatesEnabled());
        presentation->resumeAndRepaintBackground();
        QVERIFY(library.updatesEnabled());
    }

    void destructionResumesSurvivingBackground() {
        QWidget reader, background;
        reader.show();
        background.show();
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        presentation->pauseBackground();
        QVERIFY(!background.updatesEnabled());
        presentation.reset();
        QVERIFY(background.updatesEnabled());
    }

    void destroyedWindowsAreNotRetainedDuringResume() {
        auto *reader = new QWidget;
        auto *background = new QWidget;
        reader->show();
        background->show();
        auto presentation = kobalt::nickel::makeWindowPresentation(reader);
        presentation->pauseBackground();
        delete background;
        delete reader;
        presentation->pauseBackground();
        presentation->resumeAndRepaintBackground();
        presentation.reset();
    }

    void handoffCoverSurvivesDialogAcceptanceAndIsThenRemoved() {
        QWidget reader, background;
        background.resize(320, 480);
        QDialog dialog(&background);
        dialog.resize(240, 300);
        background.show();
        dialog.show();
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        QPointer<QLabel> observed;
        int calls = 0;
        presentation->acceptHandoff(dialog, [&] {
            ++calls;
            observed = background.findChild<QLabel *>("rrHandoffCover");
            QVERIFY(observed);
            QVERIFY(observed->isVisible());
            QCOMPARE(observed->geometry(), background.rect());
            dialog.accept();
            QVERIFY(observed);
            QVERIFY(observed->isVisible());
        });
        QCOMPARE(calls, 1);
        QVERIFY(observed.isNull());
        QVERIFY(!background.findChild<QLabel *>("rrHandoffCover"));
    }

    void unparentedHandoffCallsContinuationOnce() {
        QWidget reader;
        QDialog dialog;
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        int calls = 0;
        presentation->acceptHandoff(dialog, [&] {
            ++calls;
            dialog.accept();
        });
        QCOMPARE(calls, 1);
        QVERIFY(!dialog.findChild<QLabel *>("rrHandoffCover"));
    }

    void continuationCanDestroyTheBackgroundAndItsCover() {
        QWidget reader;
        auto *background = new QWidget;
        auto *dialog = new QDialog(background);
        background->show();
        dialog->show();
        auto presentation = kobalt::nickel::makeWindowPresentation(&reader);
        QPointer<QLabel> cover;
        QPointer<QDialog> guardedDialog(dialog);
        presentation->acceptHandoff(*dialog, [&] {
            cover = background->findChild<QLabel *>("rrHandoffCover");
            QVERIFY(cover);
            delete background;
        });
        QVERIFY(cover.isNull());
        QVERIFY(guardedDialog.isNull());
    }
};

QTEST_MAIN(WindowPresentationTest)
#include "window_presentation_test.moc"
