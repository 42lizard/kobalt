#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "platform/pocketbook/window.h"

#include <QApplication>
#include <QFileDialog>
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
        QImage first, second;
        for (int pass = 0; pass < 2; ++pass) {
            pocketbook::Window window(storage::Paths{profile.path()}, fonts, placeholder);
            window.resize(1404, 1872);
            window.show();
            QSignalSpy opened(&window, &pocketbook::Window::bookOpened);
            QSignalSpy failed(&window, &pocketbook::Window::bookFailed);
            if (pass == 0) {
                auto *open = window.findChild<QPushButton *>("androidOpenBook");
                QVERIFY(open);
                QTimer::singleShot(0, &window, [&] {
                    auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    QVERIFY(dialog);
                    QTest::keyPress(dialog, Qt::Key_Escape);
                    QTest::keyRelease(&window, Qt::Key_Escape);
                });
                open->click();
                QVERIFY(window.isVisible());
            }
            window.openBook(QStringLiteral(KOBALT_FIXTURE));
            QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
            QCOMPARE(failed.count(), 0);
            auto page = [&] {
                return window.grab().toImage().copy(120, 250, 1160, 1150);
            };
            QTest::qWait(300);
            if (pass == 0) {
                first = page();
                QTest::keyClick(&window, Qt::Key_PageDown);
                QTRY_VERIFY_WITH_TIMEOUT(page() != first, 10000);
                QTest::qWait(300);
                second = page();
                QLineEdit editor(&window);
                editor.setText("abc");
                editor.show();
                editor.setFocus();
                editor.setCursorPosition(2);
                QTest::keyClick(&editor, Qt::Key_Left);
                QCOMPARE(editor.cursorPosition(), 1);
            } else {
                QTRY_COMPARE_WITH_TIMEOUT(page(), second, 10000);
            }
            window.close();
            QTRY_VERIFY_WITH_TIMEOUT(window.idle(), 10000);
        }
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    PocketBookTest test;
    QTimer::singleShot(0, &app, [&] {
        app.exit(QTest::qExec(&test, argc, argv));
    });
    return app.exec();
}
#include "window_test.moc"
