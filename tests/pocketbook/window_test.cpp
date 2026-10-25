#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "dictionaries/manager.h"
#include "interfaces/reader/settings/settings_panel.h"
#include "interfaces/reader/ui/controls/paged_text.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/views/reading_page.h"
#include "platforms/pocketbook/application.h"
#include "platforms/pocketbook/window.h"
#include "reading/reading_density.h"
#include "storage/reader_storage.h"
#include "support/strings.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace kobalt;

class PocketBookTest : public QObject {
    Q_OBJECT
private slots:
    void libraryReturnsAfterReading_data() {
        QTest::addColumn<bool>("home");
        QTest::newRow("reader-return") << false;
        QTest::newRow("home-close") << true;
    }
    void libraryReturnsAfterReading() {
        QFETCH(bool, home);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        storage::Paths paths{profile.path()};
        QVERIFY(storage::prepareProfile(paths));
        const QString book = QStringLiteral(KOBALT_FIXTURE);
        {
            library::Store store(profile.path(), std::make_shared<library::LocalSource>());
            QVERIFY(store.addBook(book));
            store.save();
        }
        AssetBundle assets(QStringLiteral(KOBALT_ASSETS), QStringLiteral(KOBALT_VERSION));
        QVERIFY(ui::initializeInterfaceFonts(assets));
        FontCopies copies;
        pocketbook::Window window(paths, engineFontInputs(assets, {}, {}, {}), placeholderFontFamily(assets, copies));
        const auto cleanup = qScopeGuard([&window] {
            window.close();
            QElapsedTimer timer;
            timer.start();
            while (!window.idle() && timer.elapsed() < 10000)
                QTest::qWait(10);
        });
        window.resize(1404, 1872);
        window.show();
        auto *start = window.findChild<android::StartScreen *>();
        QVERIFY(start);
        QVERIFY(start->libraryShown());
        QSignalSpy opened(&window, &pocketbook::Window::bookOpened);
        start->bookRequested(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        auto *back = window.findChild<QPushButton *>("rrReturn");
        // Reader widgets are embedded through a QGraphicsProxyWidget.
        if (!back)
            for (auto *widget : QApplication::allWidgets())
                if (widget->objectName() == "rrReturn")
                    back = qobject_cast<QPushButton *>(widget);
        QVERIFY(back);
        ReaderWindow *reader = nullptr;
        for (auto *widget : QApplication::allWidgets())
            if (auto *candidate = dynamic_cast<ReaderWindow *>(widget); candidate && candidate->isVisible())
                reader = candidate;
        QVERIFY(reader);
        auto *page = dynamic_cast<ReadingPage *>(reader->findChild<QWidget *>("rrPage"));
        QVERIFY(page);
        for (int turn = 0; turn < 5; ++turn) {
            const auto previous = page->image;
            QTest::keyClick(&window, Qt::Key_PageDown);
            QTRY_VERIFY_WITH_TIMEOUT(reader->readyForPresentation() && page->image != previous, 10000);
        }
        if (home)
            QTest::keyClick(&window, Qt::Key_Home);
        else
            back->click();
        QTRY_VERIFY_WITH_TIMEOUT(window.idle(), 10000);
        QCOMPARE(window.isVisible(), !home);
        if (!home)
            QVERIFY(start->isVisible() && start->libraryShown());
        const auto state = storage::readBookState(paths, QUrl::fromLocalFile(book).toString());
        QCOMPARE(state.status, storage::ReadStatus::Ok);
        QVERIFY(state.object.value("percent").toInt() > 0);
        const auto expected = library::savedProgress(state.object);
        auto progressSaved = [&] {
            library::Index index;
            return index.loadDatabase(profile.path()) && index.books.contains(book) &&
                   index.books.value(book).percent == expected.percent &&
                   index.books.value(book).finished == expected.finished;
        };
        QTRY_VERIFY(progressSaved());
        QVERIFY(QFileInfo::exists(book));
        window.close();
    }
    void downloadedDictionariesSurviveReopening_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<int>("ppi");
        QTest::newRow("light-300") << false << 300;
        QTest::newRow("dark-300") << true << 300;
        QTest::newRow("light-212") << false << 212;
        QTest::newRow("dark-212") << true << 212;
    }

    void downloadedDictionariesSurviveReopening() {
        QFETCH(bool, dark);
        QFETCH(int, ppi);
        QVERIFY2(QSslSocket::supportsSsl(), "Dictionary downloads require a working TLS backend");
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        storage::Paths paths{profile.path()};
        QVERIFY(storage::writeJson(paths.preferences(), {{"dark_mode", dark}}));
        QFile archive(qEnvironmentVariable("KOBALT_DICTIONARY_FIXTURE"));
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto contents = archive.readAll();
        for (auto entry : DictionaryManager::catalog()) {
            if (entry.id != "en-en" && entry.id != "en-fr") {
                continue;
            }
            entry.size = contents.size();
            entry.sha256 = QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
            QString error;
            QVERIFY2(
                DictionaryManager::installArchive(profile.path() + "/dictionaries", entry, archive.fileName(), &error),
                qPrintable(error));
        }
        AssetBundle assets(QStringLiteral(KOBALT_ASSETS), QStringLiteral(KOBALT_VERSION));
        QVERIFY(assets.available());
        QVERIFY(ui::initializeInterfaceFonts(assets));
        for (const auto &font : bundledFontFiles(assets)) {
            QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
        }
        FontCopies copies;
        const auto fonts = engineFontInputs(assets, {}, {}, {});
        const auto placeholder = placeholderFontFamily(assets, copies);
        const auto capture = qEnvironmentVariable("KOBALT_TEST_CAPTURE");
        for (int pass = 0; pass < 2; ++pass) {
            pocketbook::Window window(paths, fonts, placeholder);
            const auto cleanup = qScopeGuard([&window] {
                window.close();
                QElapsedTimer timer;
                timer.start();
                while (!window.idle() && timer.elapsed() < 10000) {
                    QTest::qWait(10);
                }
            });
            ui::setDensity(ppi);
            readingPpi.store(ppi);
            window.resize(1404, 1872);
            window.show();
            QSignalSpy opened(&window, &pocketbook::Window::bookOpened);
            window.openBook(qEnvironmentVariable("KOBALT_LOOKUP_FIXTURE"));
            QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
            ReaderWindow *reader = nullptr;
            for (auto *widget : QApplication::allWidgets()) {
                if (auto *candidate = dynamic_cast<ReaderWindow *>(widget); candidate && candidate->isVisible()) {
                    reader = candidate;
                }
            }
            QVERIFY(reader);
            QTRY_VERIFY_WITH_TIMEOUT(reader->readyForPresentation(), 30000);
            auto *page = dynamic_cast<ReadingPage *>(reader->findChild<QWidget *>("rrPage"));
            QVERIFY(page);
            auto save = [&](const QString &scene) {
                if (capture.isEmpty()) {
                    return true;
                }
                QImage previous;
                QElapsedTimer timer;
                timer.start();
                while (timer.elapsed() < 2000) {
                    QTest::qWait(50);
                    const auto current = window.grab().toImage();
                    if (current == previous) {
                        return current.save(capture + ".dictionary-" + QTest::currentDataTag() + "-" + scene + ".png");
                    }
                    previous = current;
                }
                return false;
            };
            if (pass == 0) {
                QTest::mouseClick(page, Qt::LeftButton, Qt::NoModifier, page->rect().center());
                auto *settings = reader->findChild<QPushButton *>("rrSettings");
                QVERIFY(settings);
                QTRY_VERIFY(settings->isVisible());
                settings->click();
                auto *panel = dynamic_cast<reader::SettingsPanel *>(reader->findChild<QWidget *>("rrGeneralSettings"));
                QVERIFY(panel);
                QTRY_VERIFY(panel->isVisible());
                QVERIFY(save("settings"));
                auto *tab = panel->findChild<QPushButton *>("rrSettingsDictionaries");
                QVERIFY(tab);
                QVERIFY(tab->isVisible() && tab->isEnabled());
                tab->click();
                QTRY_VERIFY(panel->dictionaries->isVisible());
                auto rowsFit = [&] {
                    auto *row = panel->dictionaries->findChild<QWidget *>("rrDictionaryRow0");
                    auto *action = row ? row->findChild<QPushButton *>("rrDictionaryAction0") : nullptr;
                    return row && action && row->width() >= panel->dictionaries->width() - 2 &&
                           row->rect().contains(action->geometry());
                };
                QTRY_VERIFY(rowsFit());
                bool installed = false;
                for (auto *row : panel->dictionaries->findChildren<QWidget *>()) {
                    if (row->property("dictionary_id").toString() == "en-en") {
                        for (auto *action : row->findChildren<QPushButton *>()) {
                            installed |= action->text() == strings::get("dictionaries.remove");
                        }
                    }
                }
                QVERIFY(installed);
                QVERIFY(save("management"));
                settings->click();
                QTRY_VERIFY(!panel->isVisible());
                QTest::mouseClick(page, Qt::LeftButton, Qt::NoModifier, page->rect().center());
                QTRY_VERIFY(!settings->isVisible());
                QTRY_VERIFY_WITH_TIMEOUT(reader->readyForPresentation(), 10000);
            }
            QVERIFY(save("reading"));
            // Short paragraphs at 212 PPI can leave the middle of the page blank.
            // Find ink across the reading surface, avoiding the outer margins.
            QPoint at(-1, -1);
            auto inkAt = [&](int y) {
                for (int x = page->image.width() / 10; x < page->image.width() * 9 / 10; ++x) {
                    const int shade = qGray(page->image.pixel(x, y));
                    if ((dark && shade > 160) || (!dark && shade < 96)) {
                        return x;
                    }
                }
                return -1;
            };
            for (int y = page->image.height() / 3; y < page->image.height() / 2; ++y) {
                if (inkAt(y) < 0) {
                    continue;
                }
                int bottom = y;
                while (bottom + 1 < page->image.height() && inkAt(bottom + 1) >= 0) {
                    ++bottom;
                }
                const int middle = (y + bottom) / 2;
                at = QPoint(inkAt(middle) + 1, middle + page->topHeight());
                break;
            }
            QVERIFY(at.x() >= 0);
            QTRY_COMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);
            QTest::mousePress(page, Qt::LeftButton, Qt::NoModifier, at);
            QTest::qWait(ui::metrics::lookup::holdMilliseconds + 100);
            QTest::mouseRelease(page, Qt::LeftButton, Qt::NoModifier, at);
            auto *lookup = reader->findChild<QWidget *>("rrDictionary");
            QVERIFY(lookup);
            QTRY_VERIFY_WITH_TIMEOUT(lookup->isVisible(), 10000);
            auto *selector = lookup->findChild<QPushButton *>("rrDictionarySelect");
            auto *definition = dynamic_cast<ui::PagedText *>(lookup->findChild<QWidget *>("rrDefinition"));
            QVERIFY(selector && definition);
            QCOMPARE(selector->text(), pass == 0 ? QString("English monolingual") : QString("English to French"));
            if (pass == 0) {
                QVERIFY(definition->pages() > 1);
                QVERIFY(save("definition"));
                selector->click();
                auto *translation = lookup->findChild<QPushButton *>("rrDictionaryChoice1");
                QVERIFY(translation);
                QTRY_VERIFY(translation->isVisible());
                translation->click();
                QTRY_COMPARE(selector->text(), QString("English to French"));
                QCOMPARE(definition->pages(), 1);
            } else {
                QCOMPARE(definition->pages(), 1);
                QVERIFY(save("reopened"));
            }
            auto *dismiss = lookup->findChild<QPushButton *>("rrDictionaryClose");
            QVERIFY(dismiss);
            dismiss->click();
            QTRY_VERIFY(!lookup->isVisible());
            window.close();
            QTRY_VERIFY_WITH_TIMEOUT(window.idle(), 10000);
        }
    }

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
