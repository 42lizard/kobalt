#include "platform/nickel/common/browser_handoff.h"
#include <QApplication>
#include <QTest>
#include <QWidget>

class BrowserHandoffTest : public QObject {
    Q_OBJECT
private slots:
    void existingBrowserIsCoveredUntilItCloses() {
        QWidget reader, dialog;
        QWidget browser(&dialog);
        dialog.setObjectName("N3Dialog");
        browser.setObjectName("N3BrowserView");
        reader.show();
        dialog.show();
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [] {
            return true;
        });
        handoff.check();
        QVERIFY(!reader.isVisible());
        QVERIFY(kobalt::BrowserHandoff::activeFor(&reader));
        dialog.hide();
        QVERIFY(reader.isVisible());
        QVERIFY(!kobalt::BrowserHandoff::activeFor(&reader));
    }

    void unrelatedDialogDoesNotHideReader() {
        QWidget reader, dialog;
        dialog.setObjectName("N3Dialog");
        reader.show();
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [] {
            return true;
        });
        dialog.show();
        handoff.check();
        QVERIFY(reader.isVisible());
        QVERIFY(!kobalt::BrowserHandoff::activeFor(&reader));
    }

    void browserShownInsideExistingDialogStartsHandoff() {
        QWidget reader, dialog;
        QWidget container(&dialog), browser(&container);
        dialog.setObjectName("N3Dialog");
        browser.setObjectName("N3BrowserView");
        browser.hide();
        reader.show();
        dialog.show();
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [] {
            return true;
        });
        browser.show();
        QVERIFY(kobalt::BrowserHandoff::activeFor(&reader));
        QVERIFY(!reader.isVisible());
        dialog.hide();
        QVERIFY(reader.isVisible());
    }

    void sleepHideKeepsHandoffUntilBrowserClosesAfterWake() {
        QWidget reader, dialog;
        QWidget browser(&dialog);
        dialog.setObjectName("N3Dialog");
        browser.setObjectName("N3BrowserView");
        reader.show();
        bool awake = true;
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [&] {
            return awake;
        });
        dialog.show();
        awake = false;
        dialog.hide();
        QVERIFY(!reader.isVisible());
        QVERIFY(kobalt::BrowserHandoff::activeFor(&reader));
        awake = true;
        dialog.show();
        QVERIFY(!reader.isVisible());
        dialog.hide();
        QVERIFY(reader.isVisible());
        QVERIFY(!kobalt::BrowserHandoff::activeFor(&reader));
    }

    void destroyedBrowserDoesNotRestoreWhileBlocked() {
        QWidget reader;
        auto *dialog = new QWidget;
        auto *browser = new QWidget(dialog);
        dialog->setObjectName("N3Dialog");
        browser->setObjectName("N3BrowserView");
        reader.show();
        bool awake = true;
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [&] {
            return awake;
        });
        dialog->show();
        QVERIFY(kobalt::BrowserHandoff::activeFor(&reader));
        awake = false;
        delete dialog;
        QVERIFY(!reader.isVisible());
        QVERIFY(!kobalt::BrowserHandoff::activeFor(&reader));
    }

    void closingReaderCancelsItsRestoration() {
        QWidget reader, dialog;
        QWidget browser(&dialog);
        dialog.setObjectName("N3Dialog");
        browser.setObjectName("N3BrowserView");
        reader.show();
        kobalt::BrowserHandoff handoff;
        handoff.begin(&reader, [] {
            return true;
        });
        dialog.show();
        reader.close();
        QVERIFY(!kobalt::BrowserHandoff::activeFor(&reader));
        dialog.hide();
        QVERIFY(!reader.isVisible());
    }

    void destroyingReaderBeforeBrowserIsSafe() {
        auto *reader = new QWidget;
        QWidget dialog;
        QWidget browser(&dialog);
        dialog.setObjectName("N3Dialog");
        browser.setObjectName("N3BrowserView");
        reader->show();
        kobalt::BrowserHandoff handoff;
        handoff.begin(reader, [] {
            return true;
        });
        dialog.show();
        delete reader;
        dialog.hide();
        QWidget replacement;
        replacement.show();
        handoff.begin(&replacement, [] {
            return true;
        });
        dialog.show();
        QVERIFY(kobalt::BrowserHandoff::activeFor(&replacement));
        dialog.hide();
        QVERIFY(replacement.isVisible());
    }
};

QTEST_MAIN(BrowserHandoffTest)
#include "browser_handoff_test.moc"
