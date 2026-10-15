#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "platform/pocketbook/application.h"
#include "platform/pocketbook/window.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace kobalt;

class PocketBookTest : public QObject {
    Q_OBJECT
private slots:
    void platformUpdatesReachReceiver() {
        class UpdateReceiver : public QObject {
        public:
            int updates = 0;
            bool event(QEvent *event) override {
                if (event->type() == QEvent::UpdateLater) {
                    ++updates;
                    return true;
                }
                return QObject::event(event);
            }
        } receiver;
        // The firmware QPA posts plain UpdateLater events, without a QWidget region.
        QCoreApplication::postEvent(&receiver, new QEvent(QEvent::UpdateLater));
        QCoreApplication::postEvent(&receiver, new QEvent(QEvent::UpdateLater));
        QCoreApplication::sendPostedEvents(&receiver);
        QCOMPARE(receiver.updates, 2);
    }

    void pageSurvivesCloseAndReopen() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        AssetBundle assets(QStringLiteral(KOBALT_ASSETS), QStringLiteral(KOBALT_VERSION));
        QVERIFY(assets.available());
        QVERIFY(ui::initializeInterfaceFonts(assets));
        for (const auto &font : bundledFontFiles(assets)) {
            QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
        }
        FontCopies copies;
        const auto fonts = engineFontInputs(assets, {}, {}, {});
        const auto placeholder = placeholderFontFamily(assets, copies);
        const auto book = profile.path() + QStringLiteral("/Bären and Lúthien.epub");
        QVERIFY(QFile::copy(qEnvironmentVariable("KOBALT_TEST_BOOK", QStringLiteral(KOBALT_FIXTURE)), book));
        QImage first, second;
        for (int pass = 0; pass < 2; ++pass) {
            pocketbook::Window window(storage::Paths{profile.path()}, fonts, placeholder);
            window.resize(1404, 1872);
            window.show();
            QSignalSpy opened(&window, &pocketbook::Window::bookOpened);
            QSignalSpy failed(&window, &pocketbook::Window::bookFailed);
            window.openBook(book);
            QTRY_VERIFY_WITH_TIMEOUT(opened.count() || failed.count(), 30000);
            QCOMPARE(opened.count(), 1);
            QCOMPARE(failed.count(), 0);
            ReaderWindow *reader = nullptr;
            for (auto *widget : QApplication::allWidgets()) {
                if (auto *candidate = dynamic_cast<ReaderWindow *>(widget)) {
                    reader = candidate;
                }
            }
            QVERIFY(reader);
            QTRY_VERIFY_WITH_TIMEOUT(reader->readyForPresentation(), 30000);
            auto page = [&] {
                return window.grab().toImage().copy(120, 250, 1160, 1150);
            };
            if (pass == 0) {
                first = page();
                const auto capture = qEnvironmentVariable("KOBALT_TEST_CAPTURE");
                if (!capture.isEmpty()) {
                    QVERIFY(window.grab().save(capture));
                }
                QTest::keyClick(&window, Qt::Key_PageDown);
                QTRY_VERIFY_WITH_TIMEOUT(reader->readyForPresentation() && page() != first, 10000);
                second = page();
                if (!capture.isEmpty()) {
                    QVERIFY(second.save(capture + ".turned.png"));
                }
                QLineEdit editor(&window);
                editor.setText("abc");
                editor.show();
                editor.setFocus();
                editor.setCursorPosition(2);
                QTest::keyClick(&editor, Qt::Key_Left);
                QCOMPARE(editor.cursorPosition(), 1);
            } else {
                const auto capture = qEnvironmentVariable("KOBALT_TEST_CAPTURE");
                if (!capture.isEmpty()) {
                    QVERIFY(page().save(capture + ".reopened.png"));
                }
                QTRY_COMPARE_WITH_TIMEOUT(page(), second, 10000);
            }
            QPushButton *back = nullptr;
            for (auto *widget : QApplication::allWidgets()) {
                if (widget->objectName() == "rrReturn") {
                    back = qobject_cast<QPushButton *>(widget);
                }
            }
            QVERIFY(back);
            back->click();
            QTRY_VERIFY_WITH_TIMEOUT(window.idle(), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);
        }
        pocketbook::Window invalid(storage::Paths{profile.path()}, fonts, placeholder);
        invalid.resize(1404, 1735);
        invalid.show();
        QSignalSpy failed(&invalid, &pocketbook::Window::bookFailed);
        invalid.openBook(profile.path() + "/missing.epub");
        QTRY_COMPARE(failed.count(), 1);
        QTRY_VERIFY(invalid.idle());
        auto *native = invalid.findChild<QPushButton *>("rrOpeningNative");
        auto *back = invalid.findChild<QPushButton *>("rrOpeningBack");
        QVERIFY(native);
        QVERIFY(back);
        // A failed native handoff must leave the error view usable.
        native->click();
        QVERIFY(back->isVisible());
        back->click();
        QTRY_VERIFY(!invalid.isVisible());
    }
};
int main(int argc, char **argv) {
    pocketbook::Application app(argc, argv);
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    PocketBookTest test;
    QTimer::singleShot(0, &app, [&] {
        app.exit(QTest::qExec(&test, argc, argv));
    });
    return app.exec();
}
#include "window_test.moc"
