#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/views/reading_page.h"
#include "platforms/desktop/window.h"
#include "platforms/android/start_screen.h"
#include "reading/reading_density.h"
#include "storage/reader_storage.h"
#include "build_info.h"
#include "support/book_fixtures.h"
#include "support/engine_fixtures.h"
#include "support/strings.h"
#include "engine/crengine/reader_model.h"
#include "interfaces/reader/ui/controls/page_scrubber.h"
#include "interfaces/reader/settings/dictionaries_page.h"
#include <lvtinydom.h>

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <memory>

using namespace kobalt;

namespace {
AssetBundle *assets;
FontCopies *copies;
const QString book = QStringLiteral(KOBALT_TEST_BOOK);
QString captureDirectory;
QJsonArray captures;

class BlockedFile {
public:
    explicit BlockedFile(QString path) : path(std::move(path)), backup(this->path + ".saved") {
        moved = QFile::rename(this->path, backup);
        blocked = moved && QDir().mkdir(this->path);
    }
    ~BlockedFile() { restore(); }
    bool active() const { return blocked; }
    bool restore() {
        if (blocked && !QDir().rmdir(path))
            return false;
        blocked = false;
        if (moved && !QFile::rename(backup, path))
            return false;
        moved = false;
        return true;
    }
private:
    QString path, backup;
    bool moved = false, blocked = false;
};

using test::writeBook;

bool createBook(const QString &directory, const QString &filename = "text.epub") {
    // A small, offline EPUB with the same text used by the former desktop tests.
    const QString paragraph = "A reader keeps the chapter ready. Changing the size of the words should preserve "
                              "the passage. We can turn the page, visit another chapter, and return to the same place. ";
    QMap<QString, QByteArray> files;
    files["mimetype"] = "application/epub+zip";
    files["META-INF/container.xml"] =
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
        "<rootfiles><rootfile full-path=\"book.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
    QString manifest, spine, navigation;
    const bool illustrated = filename == "later-images.epub";
    const bool nonlinear = filename == "publisher-non-linear.epub";
    const bool vertical = filename == "formatting-unsupported-writing.epub";
    if (illustrated) {
        QImage image(64, 80, QImage::Format_RGB32);
        image.fill(Qt::red);
        QBuffer buffer(&files["image.png"]);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            return false;
    }
    for (int chapter = 1; chapter <= 3; ++chapter) {
        const auto name = QString("c%1.xhtml").arg(chapter);
        QString body = QString("<h1>Chapter %1</h1>").arg(chapter);
        if (nonlinear && chapter == 2)
            body += "<p>Nonlinearspine</p>";
        if (illustrated && chapter == 2)
            body += "<p><img src=\"image.png\" alt=\"A red image\"/></p>";
        for (int index = 0; index < 30; ++index) {
            body += "<p>" + paragraph.repeated(3) + "</p>";
        }
        const QString style = vertical ? " style=\"writing-mode:vertical-lr\"" : "";
        files[name] = ("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Chapter</title></head><body" + style + ">" +
                       body + "</body></html>").toUtf8();
        manifest += QString("<item id=\"c%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>").arg(chapter).arg(name);
        spine += QString("<itemref idref=\"c%1\"%2/>").arg(chapter).arg(nonlinear && chapter == 2 ? " linear=\"no\"" : "");
        navigation += QString("<navPoint id=\"n%1\" playOrder=\"%1\"><navLabel><text>Chapter %1</text>"
                              "</navLabel><content src=\"%2\"/></navPoint>").arg(chapter).arg(name);
    }
    if (illustrated)
        manifest += "<item id=\"image\" href=\"image.png\" media-type=\"image/png\"/>";
    files["book.opf"] =
        ("<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">"
         "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"uid\">kobalt-fixture</dc:identifier>"
         "<dc:title>Native reader fixture</dc:title><dc:language>en</dc:language><dc:creator>Kobalt</dc:creator></metadata>"
         "<manifest>" + manifest + "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/></manifest>"
         "<spine toc=\"ncx\">" + spine + "</spine></package>").toUtf8();
    files["toc.ncx"] =
        ("<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
         "<meta name=\"dtb:uid\" content=\"kobalt-fixture\"/></head><docTitle><text>Native reader fixture</text>"
         "</docTitle><navMap>" + navigation + "</navMap></ncx>").toUtf8();
    return writeBook(directory, filename, files);
}

struct Fixture {
    QTemporaryDir directory{QDir::tempPath() + "/kobalt-profile-XXXXXX"};
    storage::Paths paths{directory.path()};
    bool prepared = false;
    std::atomic_int clockReads{0};
    const EngineFontInputs fontInputs;
    desktop::Window window;

    explicit Fixture(bool dark = false, bool variableFont = false)
        : fontInputs(engineFontInputs(*assets, {}, variableFont ? directory.path() + "/fonts" : QString(), {})),
          window(prepare(dark, variableFont), fontInputs, placeholderFontFamily(*assets, *copies),
                 [this] {
                     ++clockReads;
                     return QStringLiteral("09:00");
                 }) {
        window.resize(920, 800);
        window.show();
    }
    storage::Paths prepare(bool dark, bool variableFont) {
        paths.stateReport = directory.path() + "/state.json";
        prepared = directory.isValid() && storage::writeJson(paths.preferences(), {{"dark_mode", dark}});
        if (variableFont)
            prepared = prepared && QDir().mkpath(directory.path() + "/fonts") &&
                       QFile::copy(QStringLiteral(KOBALT_TEST_VARIABLE_FONT), directory.path() + "/fonts/Amstelvar.ttf");
        return paths;
    }
    ~Fixture() {
        window.close();
        if (!QTest::qWaitFor([this] { return window.idle(); }, 30000)) {
            QTest::qFail("Reader did not drain while closing", __FILE__, __LINE__);
        }
    }
    QJsonObject state() {
        if (auto *reader = window.readerWindow()) {
            reader->checkpoint();
        }
        return storage::readJson(paths.stateReport);
    }
    bool click(QWidget *target, QPoint position = QPoint(-1, -1)) {
        auto *reader = window.readerWindow();
        auto *canvas = window.findChild<QGraphicsView *>();
        if (!reader || !canvas || !target || !target->isVisible() || !target->isEnabled()) {
            return false;
        }
        if (position.x() < 0) {
            position = target->rect().center();
            // A taller typography panel can cover the page's center. Use
            // exposed paper to dismiss it, as a person would, and never send
            // a page tap to an unrelated control above it.
            if (target->objectName() == "rrPage") {
                for (int y : {position.y(), target->height() / 3, target->height() / 4}) {
                    const QPoint candidate(position.x(), y);
                    if (reader->childAt(target->mapTo(reader, candidate)) == target) {
                        position = candidate;
                        break;
                    }
                }
                if (reader->childAt(target->mapTo(reader, position)) != target)
                    return false;
            }
        }
        const auto point = canvas->mapFromScene(target->mapTo(reader, position));
        if (!canvas->viewport()->rect().contains(point)) {
            return false;
        }
        QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        return true;
    }
    QWidget *widget(const char *name) {
        auto *reader = window.readerWindow();
        return reader ? reader->findChild<QWidget *>(name) : nullptr;
    }
    bool press(const char *name) { return click(widget(name)); }
    bool hold(QWidget *target, QPoint position) {
        auto *reader = window.readerWindow();
        auto *canvas = window.findChild<QGraphicsView *>();
        if (!reader || !canvas || !target)
            return false;
        const auto at = canvas->mapFromScene(target->mapTo(reader, position));
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        const bool ready = QTest::qWaitFor([this] { return visible("rrLinkGo"); }, 2000);
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        return ready;
    }
    bool visible(const char *name) {
        const auto *item = widget(name);
        return item && item->isVisible();
    }
    QString storedPath(const QString &filename) {
        QDirIterator files(paths.annotations(), {filename}, QDir::Files, QDirIterator::Subdirectories);
        return files.hasNext() ? files.next() : QString();
    }
    QJsonArray stored(const QString &filename, const QString &key) {
        return storage::readJson(storedPath(filename)).value(key).toArray();
    }
    QPoint wordPoint(int ordinal = 0) {
        // Use the engine's geometry to target real text through the canvas.
        // The test never reaches into the reader window's private session.
        auto *page = static_cast<ReadingPage *>(widget("rrPage"));
        if (!page)
            return {-1, -1};
        const auto current = state();
        QFile input(book);
        ReaderModel coordinates(fontInputs);
        QString error;
        if (!input.open(QIODevice::ReadOnly) ||
            !coordinates.open(input.readAll(), page->image.size(), current, &error) ||
            !coordinates.goTo(current.value("position").toString()))
            return {-1, -1};
        QSet<QString> seen;
        for (int y = page->image.height() / 3; y < page->image.height() * 2 / 3; y += 12)
            for (int x = page->image.width() / 3; x < page->image.width() * 2 / 3; x += 12) {
                const auto word = coordinates.wordAt({x, y});
                if (!word.rectangles.isEmpty() && !seen.contains(word.start)) {
                    seen.insert(word.start);
                    if (ordinal-- == 0)
                        return word.rectangles.first().center() + QPoint(0, page->topHeight());
                }
            }
        return {-1, -1};
    }
    QPoint linkPoint(const QString &path, const QString &label) {
        auto *page = static_cast<ReadingPage *>(widget("rrPage"));
        if (!page)
            return {-1, -1};
        const auto current = state();
        QFile input(path);
        ReaderModel coordinates(fontInputs);
        QString error;
        if (!input.open(QIODevice::ReadOnly) ||
            !coordinates.open(input.readAll(), page->image.size(), current, &error) ||
            !coordinates.goTo(current.value("position").toString()))
            return {-1, -1};
        for (const auto &link : coordinates.pageLinks())
            if (coordinates.linkDetails(link.source).label == label && !link.rectangles.isEmpty())
                return link.rectangles.first().center() + QPoint(0, page->topHeight());
        return {-1, -1};
    }
};

void variants() {
    QTest::addColumn<int>("preset");
    QTest::addColumn<bool>("dark");
    for (int preset : {0, 1, 2}) {
        for (bool dark : {false, true}) {
            QTest::newRow(qPrintable(QString("%1-%2").arg(preset).arg(dark ? "dark" : "light"))) << preset << dark;
        }
    }
}

bool hasText(const QImage &source) {
    const auto image = source.convertToFormat(QImage::Format_ARGB32);
    int paper = 0, ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) != 255) {
                continue;
            }
            paper += qGray(row[x]) > 180;
            ink += qGray(row[x]) < 80;
        }
    }
    return paper > 100 && ink > 100;
}

bool capture(Fixture &fixture, const QString &name) {
    // Model state and visibility can settle before queued layouts and proxy
    // cache updates. Deliver those updates before rendering the visible frame.
    QCoreApplication::processEvents();
    QString error;
    const auto path = fixture.window.saveScreenshot(captureDirectory, &error);
    const auto destination = captureDirectory + '/' + name + ".png";
    if (path.isEmpty() || !QFile::rename(path, destination)) {
        qCritical() << "Cannot save screenshot:" << name << error;
        return false;
    }
    const QImage image(destination);
    QFile file(destination);
    if (image.isNull() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    captures.append(QJsonObject{{"file", name + ".png"}, {"width", image.width()}, {"height", image.height()},
                               {"sha256", QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex())}});
    return true;
}

void advanceIntoBody(Fixture &fixture) {
    // Capture narrative text as well as the chapter-opening typography.
    auto *reader = fixture.window.readerWindow();
    QVERIFY(reader);
    const int chapterStart = fixture.state().value("page").toInt();
    reader->pageButton(1);
    QTRY_VERIFY_WITH_TIMEOUT(fixture.state().value("page").toInt() > chapterStart, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
    auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
    QVERIFY(page && hasText(page->image));
}

void openPrologueBody(Fixture &fixture) {
    if (!fixture.visible("rrNavChapters"))
        QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrNavChapters"));
    QVERIFY(fixture.press("rrNavChapters"));
    QTRY_VERIFY(fixture.visible("rrChapter1"));
    QVERIFY(fixture.press("rrChapter1"));
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
    advanceIntoBody(fixture);
}

// The same real-widget tour supplies integration checks and screenshot scenes.
void tour(Fixture &fixture, const QString &name, bool screenshots) {
    const auto scene = [&](const QString &suffix) { return !screenshots || capture(fixture, name + '-' + suffix); };
    QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrSettings"));
    QVERIFY(scene("menu"));
    QVERIFY(fixture.press("rrNavChapters"));
    QTRY_VERIFY(fixture.visible("rrContents"));
    QVERIFY(fixture.widget("rrChapter2"));
    QVERIFY(scene("chapters"));
    QVERIFY(fixture.press("rrChapter1"));
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
    QVERIFY(fixture.state().value("page").toInt() > 0);
    advanceIntoBody(fixture);
    QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrNavText"));
    QVERIFY(fixture.press("rrNavStats"));
    QTRY_VERIFY(fixture.visible("rrChapterGraph"));
    QVERIFY(scene("stats"));
    QVERIFY(fixture.press("rrNavStats"));
    QTRY_VERIFY(!fixture.visible("rrChapterGraph"));
    QVERIFY(fixture.press("rrNavText"));
    QTRY_VERIFY(fixture.visible("rrFont"));
    QVERIFY(scene("text"));
    QVERIFY(fixture.press("rrFont"));
    QTRY_VERIFY(fixture.visible("rrFontChoice0"));
    QVERIFY(scene("fonts"));
    const auto originalTypography = fixture.state();
    const int originalSize = originalTypography.value("font_pixels").toInt();
    if (!screenshots)
        QVERIFY(fixture.press("rrFontChoice1"));
    QVERIFY(fixture.press("rrFontClose"));
    QTRY_VERIFY(!fixture.visible("rrChoices"));
    if (!screenshots) {
        QVERIFY(fixture.press("rrLarger"));
        QVERIFY(fixture.press("rrHeavier"));
        QVERIFY(fixture.press("rrLooser"));
        QVERIFY(fixture.press("rrAlignLeft"));
        QVERIFY(fixture.press("rrHyphenation_on"));
        QVERIFY(!fixture.visible("rrOpticalSize")); // The bundled faces are static.
    }
    QVERIFY(fixture.press("rrNavLayout"));
    QTRY_VERIFY(fixture.visible("rrHeaderFooter"));
    if (!screenshots)
        QVERIFY(fixture.press("rrWider"));
    QVERIFY(scene("layout"));
    QVERIFY(fixture.press("rrHeaderFooter"));
    QTRY_VERIFY(fixture.visible("rrHeaderFooterView"));
    QVERIFY(scene("header-footer"));
    if (!screenshots)
        QVERIFY(fixture.press("rrHeaderFooterPreset_alternative_a"));
    QVERIFY(fixture.press("rrHeaderFooterClose"));
    QTRY_VERIFY(!fixture.visible("rrHeaderFooterView"));
    QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
    QTRY_VERIFY(!fixture.visible("rrMenu"));
    if (!screenshots) {
        QTRY_VERIFY_WITH_TIMEOUT(fixture.state().value("font_pixels").toInt() > originalSize, 30000);
        QVERIFY(!fixture.state().value("font_face").toString().isEmpty());
        QVERIFY(fixture.state().value("weight_steps").toInt() > originalTypography.value("weight_steps").toInt());
        QVERIFY(fixture.state().value("line_spacing").toInt() > originalTypography.value("line_spacing").toInt());
        QVERIFY(fixture.state().value("margin_pixels").toInt() > originalTypography.value("margin_pixels").toInt());
        QCOMPARE(fixture.state().value("text_alignment").toString(), QString("left"));
        QCOMPARE(fixture.state().value("hyphenation").toString(), QString("on"));
        QCOMPARE(fixture.state().value("header_footer_preset").toString(), QString("alternative_a"));
    }
    auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
    QVERIFY(page);
    // Model idleness precedes the UI's appearance-apply continuation. The
    // toolbar disappears and Graph is enabled when reading accepts input.
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.visible("rrControlsTop") && fixture.widget("rrGraph")->isEnabled(), 30000);
    QVERIFY(fixture.click(page, page->bookmarkCorner().center()));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.state().value("bookmarked").toBool(), 3000);
    QVERIFY(scene("bookmarked"));
    if (screenshots) {
        // Creation dates are visible in the list. Prepare a fixed timestamp
        // on disk, then let the app load it normally before capturing it.
        QSignalSpy reopened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.readerWindow()->close();
        QTRY_VERIFY(fixture.window.idle());
        const auto path = fixture.storedPath("bookmarks.json");
        auto stored = storage::readJson(path);
        auto entries = stored.value("bookmarks").toArray();
        for (int i = 0; i < entries.size(); ++i) {
            auto entry = entries.at(i).toObject();
            entry["created"] = "2000-01-01T12:00:00Z";
            entries[i] = entry;
        }
        stored["bookmarks"] = entries;
        QVERIFY(storage::writeJson(path, stored));
        fixture.window.openBook(book);
        QTRY_COMPARE(reopened.count(), 1);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QTRY_VERIFY(fixture.state().value("bookmarked").toBool());
        page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
        QVERIFY(page);
    }
    QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrBookmarks"));
    QVERIFY(fixture.press("rrBookmarks"));
    QTRY_VERIFY(fixture.visible("rrBookmark0"));
    QVERIFY(scene("bookmarks"));
    if (screenshots) {
        QVERIFY(fixture.press("rrBookmarkRemove0"));
        QTRY_VERIFY(fixture.visible("rrRemoveBookmark"));
        QVERIFY(scene("bookmark-remove"));
        QVERIFY(fixture.press("rrKeepBookmark"));
        QTRY_VERIFY(!fixture.visible("rrRemoveBookmark"));
    }
    QVERIFY(fixture.press("rrAnnotationsTab"));
    QTRY_VERIFY(fixture.visible("rrBookmarkListEmpty"));
    QVERIFY(scene("annotations"));
    QVERIFY(fixture.press("rrBookmarkListClose"));
    if (!fixture.visible("rrSearchOpen"))
        QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrSearchOpen"));
    QVERIFY(fixture.press("rrSearchOpen"));
    QTRY_VERIFY(fixture.visible("rrSearchQuery"));
    auto *query = fixture.window.readerWindow()->findChild<QLineEdit *>("rrSearchQuery");
    QVERIFY(query);
    query->setText("Trevelyan");
    QVERIFY(fixture.press("rrSearchSubmit"));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.visible("rrSearchResult0"), 30000);
    auto *status = fixture.window.readerWindow()->findChild<QLabel *>("rrSearchStatus");
    QVERIFY(status);
    QTRY_VERIFY_WITH_TIMEOUT(!status->text().contains("Searching", Qt::CaseInsensitive), 30000);
    QVERIFY(scene("search"));
    QVERIFY(fixture.press("rrSearchResult0"));
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
    QTRY_VERIFY(!fixture.visible("rrSearch"));
    if (!fixture.visible("rrSettings"))
        QVERIFY(fixture.press("rrPage"));
    QTRY_VERIFY(fixture.visible("rrSettings"));
    QVERIFY(fixture.press("rrSettings"));
    QTRY_VERIFY(fixture.visible("rrSettingsPages"));
    QVERIFY(scene("settings"));
    const bool fill = fixture.window.findChild<QComboBox *>("desktopScreenPicker")->currentIndex() == 0;
    QCOMPARE(fixture.visible("rrSettingsDictionaries"), fill);
    QCOMPARE(fixture.visible("rrSettingsUI"), fill);
    if (fill) {
        QVERIFY(fixture.press("rrSettingsDictionaries"));
        if (screenshots) {
            // Disk space is a host input, like the clock. Keep it fixed in
            // the screenshot fixture while retaining the real catalogue UI.
            auto *dictionaries = dynamic_cast<reader::DictionariesPage *>(fixture.widget("rrDictionaries"));
            QVERIFY(dictionaries);
            dictionaries->setStorage({}, 32000000000LL);
        }
        QTRY_VERIFY(fixture.visible("rrDictionaryRow0"));
        QTRY_VERIFY(fixture.widget("rrDictionaryRow0")->width() == fixture.widget("rrDictionaryRow0")->parentWidget()->width());
        QVERIFY(fixture.widget("rrDictionaryRow0")->width() > 200);
        QVERIFY(scene("dictionaries"));
        QVERIFY(fixture.press("rrSettingsUI"));
        QVERIFY(scene("ui"));
    }
    QVERIFY(fixture.press("rrSettingsAbout"));
    QTRY_VERIFY(fixture.visible("rrAboutVersion"));
    QVERIFY(scene("about"));
    QVERIFY(fixture.press("rrSettings"));
    QTRY_VERIFY(!fixture.visible("rrGeneralSettings"));
    if (screenshots) {
        openPrologueBody(fixture);
        QVERIFY(fixture.press("rrPage"));
    }
    QTRY_VERIFY(fixture.visible("rrAnnotate"));
    QVERIFY(fixture.press("rrAnnotate"));
    QTRY_VERIFY(fixture.visible("rrAnnotationHeader"));
    QVERIFY(scene("annotating"));
    if (screenshots) {
        const auto word = fixture.wordPoint();
        QVERIFY(word.x() >= 0);
        QVERIFY(fixture.click(page, word));
        auto *hint = fixture.window.readerWindow()->findChild<QLabel *>("rrAnnotationHint");
        QVERIFY(hint);
        QTRY_COMPARE(hint->text(), strings::get("annotations.tap_end"));
        QVERIFY(scene("annotation-start"));
        QVERIFY(fixture.click(page, word));
        QTRY_VERIFY(fixture.visible("rrAnnotationHighlightWord"));
        QVERIFY(scene("annotation-actions"));
        QVERIFY(fixture.press("rrAnnotationHighlightWord"));
        QTRY_COMPARE(fixture.stored("highlights.json", "highlights").size(), 1);
        QTRY_VERIFY(!fixture.visible("rrAnnotationPopover"));
        QVERIFY(scene("annotation-saved"));
        QVERIFY(fixture.click(page, word));
        QTRY_VERIFY(fixture.visible("rrAnnotationEdit"));
        QVERIFY(scene("annotation-edit"));
    }
    QVERIFY(fixture.press("rrAnnotationClose"));
    QTRY_VERIFY(!fixture.visible("rrAnnotationHeader"));
}
}

class DesktopTests final : public QObject {
    Q_OBJECT
private slots:
    void diagnostics() {
        // Capture native stdout/stderr too: Qt's message handler does not see
        // libpng errors or CREngine's direct printf diagnostics.
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {"startup", "covers", "reading:0-light", "errors", "features"});
        // Leave time to kill and reap the child before the CMake parent's limit.
        const bool finished = child.waitForFinished(20000);
        if (!finished) {
            child.kill();
            child.waitForFinished();
        }
        const auto output = child.readAll();
        QVERIFY2(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, output.constData());
        QVERIFY2(!output.contains("libpng error:"), output.constData());
        QVERIFY2(!output.contains("CRE WARNING:"), output.constData());
        QVERIFY2(!output.contains("styles re-init needed"), output.constData());
    }
    void storageDiagnostics() {
        // ldomDocument unregisters its fonts on destruction, even when empty.
        struct Fonts {
            bool owned = !fontMan;
            ~Fonts() { if (owned) ShutdownFontManager(); }
        } fonts;
        if (fonts.owned)
            QVERIFY(InitFontManager(lString8()));
        class Storage final : public ldomDataStorageManager {
        public:
            Storage(ldomDocument &document, lUInt32 limit)
                : ldomDataStorageManager(&document, 't', limit, 65536) {}
            bool warned() const { return _maxSizeReachedWarned; }
        };
        ldomDocument document;
        Storage normal(document, 1024 * 1024);
        const auto address = normal.allocText(16, 0, lString8("A stored paragraph"));
        normal.compact(0xFFFFFF);
        QVERIFY(!normal.warned());
        QCOMPARE(QByteArray(normal.getText(address).c_str()), QByteArray("A stored paragraph"));
        // Actual pressure still warns, even after a deliberate compaction.
        Storage limited(document, 1);
        limited.compact(0xFFFFFF);
        QVERIFY(!limited.warned());
        limited.allocText(16, 0, lString8("A stored paragraph"));
        QVERIFY(limited.getUncompressedSize() > 1);
        limited.compact(0);
        QVERIFY(limited.warned());
    }
    void startup() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        QCOMPARE(fixture.window.findChild<QComboBox *>("desktopScreenPicker")->currentIndex(), 1);
        QCOMPARE(fixture.window.findChild<QComboBox *>("desktopResolutionPicker")->currentData().toInt(), 300);
        QVERIFY(!fixture.window.readerWindow());
        QVERIFY(fixture.window.findChild<QPushButton *>("androidOpenBook"));
    }
    void covers() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        auto *start = fixture.window.findChild<android::StartScreen *>();
        QVERIFY(start);
        auto *card = start->findChild<QPushButton *>("androidLastBook");
        QVERIFY(card);
        const auto hasCover = [card] {
            const auto image = card->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int colored = 0;
            for (int y = 0; y < image.height(); ++y) {
                const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    colored += row[x] == qRgb(255, 0, 255);
            }
            return colored > 100;
        };
        QJsonObject recent{{"path", book}, {"title", "A book without a cover"}};
        start->setLastBook(recent);
        QTRY_VERIFY(card->isVisible());
        QVERIFY(!hasCover());
        QImage cover(20, 30, QImage::Format_RGB32);
        cover.fill(Qt::magenta);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(cover.save(&buffer, "PNG"));
        recent["cover"] = QString::fromLatin1(bytes.toBase64());
        start->setLastBook(recent);
        QTRY_VERIFY(hasCover());
        recent.remove("cover");
        start->setLastBook(recent);
        QVERIFY(!hasCover());
    }
    void reading_data() { variants(); }
    void reading() {
        QFETCH(int, preset);
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        fixture.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        auto *reader = fixture.window.readerWindow();
        QVERIFY(reader);
        QVERIFY(fixture.clockReads.load() > 0);
        QCOMPARE(reader->property("reader_dark").toBool(), dark);
        QVERIFY(fixture.state().value("pages").toInt() > 2);
        openPrologueBody(fixture);
        QVERIFY(hasText(reader->findChild<QWidget *>("rrPage")->grab().toImage()));
        const int page = fixture.state().value("page").toInt();
        reader->pageButton(1);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.state().value("page").toInt() > page, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        const auto position = fixture.state().value("position").toString();
        QVERIFY(!position.isEmpty());
        QVERIFY(fixture.click(reader->findChild<QWidget *>("rrPage")));
        auto *exit = reader->findChild<QPushButton *>("rrReturn");
        QTRY_VERIFY(exit->isVisible());
        QVERIFY(fixture.click(exit));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.window.idle(), 30000);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        QCOMPARE(fixture.state().value("position").toString(), position);
    }
    void errors() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        QSignalSpy failed(&fixture.window, &desktop::Window::bookFailed);
        fixture.window.openBook(fixture.directory.path() + "/missing.epub");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.window.idle(), 30000);
        fixture.window.openBook(book);
        fixture.window.close();
        QTRY_VERIFY_WITH_TIMEOUT(fixture.window.idle(), 30000);
        QVERIFY(!fixture.window.isVisible());
    }
    void features() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        tour(fixture, {}, false);
    }
    void bookmarks_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void bookmarks() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
        QVERIFY(page);
        QVERIFY(fixture.click(page, page->bookmarkCorner().center()));
        QTRY_COMPARE(fixture.state().value("bookmark_count").toInt(), 1);
        const auto original = fixture.stored("bookmarks.json", "bookmarks");
        QCOMPARE(original.size(), 1);
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrBookmarks"));
        QVERIFY(fixture.press("rrBookmarks"));
        QTRY_VERIFY(fixture.visible("rrBookmarkRemove0"));
        QVERIFY(fixture.press("rrBookmarkRemove0"));
        QTRY_VERIFY(fixture.visible("rrRemoveBookmark"));
        QCOMPARE(fixture.stored("bookmarks.json", "bookmarks"), original);
        QVERIFY(fixture.press("rrKeepBookmark"));
        QTRY_VERIFY(!fixture.visible("rrRemoveBookmark"));
        QCOMPARE(fixture.stored("bookmarks.json", "bookmarks"), original);
        QVERIFY(fixture.press("rrBookmarkRemove0"));
        QTRY_VERIFY(fixture.visible("rrConfirmRemoveBookmark"));
        QVERIFY(fixture.press("rrConfirmRemoveBookmark"));
        QTRY_VERIFY(!fixture.visible("rrRemoveBookmark"));
        QTRY_COMPARE(fixture.state().value("bookmark_count").toInt(), 0);
        QVERIFY(fixture.stored("bookmarks.json", "bookmarks").isEmpty());
        QVERIFY(!page->bookmarked);
        QVERIFY(fixture.visible("rrBookmarkListEmpty"));
        QVERIFY(fixture.press("rrBookmarkListClose"));
        if (!fixture.visible("rrReturn"))
            QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrReturn"));
        // Closing the list intentionally guards Exit against the release
        // of the tap that dismissed it. Wait for that defined input interval.
        QTest::qWait(ui::metrics::contents::exitGuardMilliseconds + 20);
        QVERIFY(fixture.press("rrReturn"));
        QTRY_VERIFY(fixture.window.idle());
        fixture.window.openBook(book);
        QTRY_COMPARE(opened.count(), 2);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QCOMPARE(fixture.state().value("bookmark_count").toInt(), 0);
    }
    void annotations_data() { bookmarks_data(); }
    void bookmarkMerge() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
        QVERIFY(page);
        for (int index = 0; index < 2; ++index) {
            QVERIFY(fixture.click(page, page->bookmarkCorner().center()));
            QTRY_COMPARE(fixture.state().value("bookmark_count").toInt(), index + 1);
            if (!index) {
                const int before = fixture.state().value("page").toInt();
                fixture.window.readerWindow()->pageButton(1);
                QTRY_VERIFY(!fixture.state().value("busy").toBool() && fixture.state().value("page").toInt() > before);
            }
        }
        const int originalSize = fixture.state().value("font_pixels").toInt();
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrNavText"));
        QVERIFY(fixture.press("rrNavText"));
        QTRY_VERIFY(fixture.visible("rrTextSize"));
        auto *size = fixture.widget("rrTextSize");
        QVERIFY(fixture.click(size, {0, size->height() / 2}));
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QTRY_VERIFY(fixture.state().value("font_pixels").toInt() < originalSize);
        QTRY_COMPARE(fixture.state().value("bookmark_count").toInt(), 1);
        QCOMPARE(fixture.stored("bookmarks.json", "bookmarks").size(), 1);
        QTRY_VERIFY(fixture.state().value("bookmarked").toBool());
        QTRY_VERIFY(!fixture.visible("rrControlsTop") && fixture.widget("rrGraph")->isEnabled());
        QVERIFY(fixture.click(page, page->bookmarkCorner().center()));
        QTRY_COMPARE(fixture.state().value("bookmark_count").toInt(), 0);
        QVERIFY(fixture.stored("bookmarks.json", "bookmarks").isEmpty());
    }
    void annotations() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        const QPoint word = fixture.wordPoint();
        QVERIFY(word.x() >= 0);
        const QPoint end = fixture.wordPoint(1);
        QVERIFY(end.x() >= 0 && end != word);
        auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
        const auto position = fixture.state().value("position");
        const auto image = page->image;
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrAnnotate"));
        QVERIFY(fixture.press("rrAnnotate"));
        QTRY_VERIFY(fixture.visible("rrAnnotationHeader"));
        QVERIFY(fixture.click(page, word));
        auto *hint = fixture.window.readerWindow()->findChild<QLabel *>("rrAnnotationHint");
        QVERIFY(hint);
        QTRY_COMPARE(hint->text(), strings::get("annotations.tap_end"));
        QVERIFY(fixture.click(page, word));
        QTRY_VERIFY(fixture.visible("rrAnnotationHighlightWord"));
        QVERIFY(fixture.press("rrAnnotationHighlightWord"));
        QTRY_COMPARE(fixture.stored("highlights.json", "highlights").size(), 1);
        const auto saved = fixture.stored("highlights.json", "highlights").first().toObject();
        QVERIFY(!saved.value("text").toString().isEmpty());
        QVERIFY(!saved.value("start").toString().isEmpty());
        QVERIFY(!saved.value("end").toString().isEmpty());
        QCOMPARE(fixture.state().value("position"), position);
        QCOMPARE(page->image, image);
        QVERIFY(fixture.click(page, word));
        QTRY_VERIFY(fixture.visible("rrAnnotationEdit"));
        QTRY_VERIFY(fixture.visible("rrAnnotationColor_blue"));
        {
            BlockedFile blocked(fixture.storedPath("highlights.json"));
            QVERIFY(blocked.active());
            QVERIFY(fixture.press("rrAnnotationColor_blue"));
            QTRY_VERIFY(fixture.visible("rrAnnotationPaletteError"));
            QVERIFY(!fixture.window.readerWindow()->findChild<QLabel *>("rrAnnotationPaletteError")->text().isEmpty());
            QVERIFY(blocked.restore());
        }
        QCOMPARE(fixture.stored("highlights.json", "highlights").first().toObject(), saved);
        QVERIFY(fixture.press("rrAnnotationColor_blue"));
        QTRY_COMPARE(fixture.stored("highlights.json", "highlights").first().toObject().value("color").toString(), QString("blue"));
        QCOMPARE(fixture.stored("highlights.json", "highlights").first().toObject().value("id"), saved.value("id"));
        QVERIFY(fixture.press("rrAnnotationEdit"));
        QVERIFY(fixture.click(page, end));
        QTRY_VERIFY(fixture.visible("rrAnnotationEnd"));
        QVERIFY(fixture.press("rrAnnotationEnd"));
        QTRY_VERIFY(!fixture.visible("rrAnnotationPopover"));
        QCOMPARE(fixture.stored("highlights.json", "highlights").size(), 1);
        QTRY_VERIFY(fixture.stored("highlights.json", "highlights").first().toObject().value("text") != saved.value("text"));
        QVERIFY(fixture.stored("highlights.json", "highlights").first().toObject().value("text").toString().contains(saved.value("text").toString()));
        QCOMPARE(fixture.stored("highlights.json", "highlights").first().toObject().value("id"), saved.value("id"));
        QVERIFY(fixture.press("rrAnnotationClose"));
        QTRY_VERIFY(!fixture.visible("rrAnnotationHeader"));
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrBookmarks"));
        QVERIFY(fixture.press("rrBookmarks"));
        QVERIFY(fixture.press("rrAnnotationsTab"));
        QTRY_VERIFY(fixture.visible("rrAnnotationRemove0"));
        QVERIFY(fixture.press("rrAnnotationRemove0"));
        QTRY_VERIFY(fixture.visible("rrKeepAnnotation"));
        QVERIFY(fixture.press("rrKeepAnnotation"));
        QTRY_VERIFY(!fixture.visible("rrRemoveAnnotation"));
        QCOMPARE(fixture.stored("highlights.json", "highlights").size(), 1);
        QVERIFY(fixture.press("rrAnnotationRemove0"));
        QTRY_VERIFY(fixture.visible("rrConfirmRemoveAnnotation"));
        QVERIFY(fixture.press("rrConfirmRemoveAnnotation"));
        QTRY_COMPARE(fixture.stored("highlights.json", "highlights").size(), 0);
        QVERIFY(fixture.visible("rrBookmarkListEmpty"));
    }
    void links_data() { bookmarks_data(); }
    void links() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        QVERIFY(writeBook(fixture.directory.path(), "links.epub", test::linkBook()));
        const auto path = fixture.directory.path() + "/links.epub";
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(path);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        auto *page = fixture.widget("rrPage");
        const auto origin = fixture.state().value("position");
        const auto note = fixture.linkPoint(path, "short note");
        QVERIFY(note.x() >= 0);
        QVERIFY(fixture.hold(page, note));
        QCOMPARE(fixture.state().value("position"), origin);
        QVERIFY(fixture.press("rrLinkGo"));
        QTRY_VERIFY(fixture.visible("rrLinkPanel"));
        QVERIFY(fixture.press("rrLinkClose"));
        QTRY_VERIFY(!fixture.visible("rrLinkPanel"));
        QCOMPARE(fixture.state().value("position"), origin);
        const auto passage = fixture.linkPoint(path, "later passage");
        QVERIFY(passage.x() >= 0);
        QVERIFY(fixture.hold(page, passage));
        QVERIFY(fixture.press("rrLinkGo"));
        QTRY_VERIFY(!fixture.state().value("busy").toBool() && fixture.state().value("position") != origin);
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrScrubberReturn"));
        QVERIFY(fixture.press("rrScrubberReturn"));
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QCOMPARE(fixture.state().value("position"), origin);
        if (fixture.visible("rrControlsTop"))
            QVERIFY(fixture.press("rrPage"));
        const auto website = fixture.linkPoint(path, "website");
        QVERIFY(website.x() >= 0);
        QVERIFY(fixture.hold(page, website));
        QVERIFY(fixture.press("rrLinkGo"));
        QTRY_VERIFY(fixture.visible("rrBrowserConfirmation"));
        QCOMPARE(fixture.state().value("position"), origin);
        // Cancel through the actual reader. External browser dispatch is
        // checked separately by the component's controlled browser service.
        QVERIFY(fixture.press("rrLinkNo"));
        QTRY_VERIFY(!fixture.visible("rrBrowserConfirmation"));
        QCOMPARE(fixture.state().value("position"), origin);
    }
    void settings_data() { bookmarks_data(); }
    void dictionaryRedraw_data() { bookmarks_data(); }
    void dictionaryRedraw() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        fixture.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(0);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrSettings"));
        QVERIFY(fixture.press("rrSettings"));
        QVERIFY(fixture.press("rrSettingsDictionaries"));
        QTRY_VERIFY(fixture.visible("rrDictionaryRow0"));
        auto *dictionaries = dynamic_cast<reader::DictionariesPage *>(fixture.widget("rrDictionaries"));
        QVERIFY(dictionaries);
        dictionaries->setStorage({}, 32000000000LL);
        QCoreApplication::processEvents();
        auto *row = fixture.widget("rrDictionaryRow0");
        QVERIFY(row);
        bool found = false;
        for (auto *label : row->findChildren<QLabel *>()) {
            if (!label->text().contains("English"))
                continue;
            found = true;
            QCOMPARE(label->palette().color(QPalette::WindowText), dark ? QColor(Qt::white) : QColor(Qt::black));
            auto *canvas = fixture.window.findChild<QGraphicsView *>();
            auto *stage = fixture.window.centralWidget();
            const auto at = [&](QPoint point) {
                return canvas->viewport()->mapTo(stage, canvas->mapFromScene(label->mapTo(fixture.window.readerWindow(), point)));
            };
            const QImage frame(fixture.window.saveScreenshot(fixture.directory.path()));
            QVERIFY(!frame.isNull());
            const qreal scale = frame.devicePixelRatio();
            const QRect bounds(at(label->rect().topLeft()) * scale, at(label->rect().bottomRight()) * scale);
            QVERIFY(frame.rect().contains(bounds));
            int ink = 0;
            const auto pixels = frame.convertToFormat(QImage::Format_ARGB32);
            for (int y = bounds.top() + 2; y < bounds.bottom() - 2; ++y)
                for (int x = bounds.left() + 2; x < bounds.right() - 2; ++x) {
                    const auto pixel = pixels.pixel(x, y);
                    const int gray = qGray(pixel);
                    ink += qAlpha(pixel) > 200 && (dark ? gray > 200 : gray < 80);
                }
            QVERIFY2(ink > 10, "Dictionary title disappeared from the actual canvas capture after rebuilding the list");
        }
        QVERIFY(found);
    }
    void settings() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        fixture.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(0);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrSettings"));
        QVERIFY(fixture.press("rrSettings"));
        struct Option { const char *tab, *name, *key; };
        QJsonObject expected;
        for (const auto &option : {Option{"rrSettingsControls", "rrShowLinkIndicator", "show_link_indicator"},
                                   Option{"rrSettingsDefaults", "rrInheritTextLayout", "inherit_text_layout"},
                                   Option{"rrSettingsUI", "rrHideStatusBar", "native_hide_status_bar"}}) {
            QVERIFY(fixture.press(option.tab));
            QTRY_VERIFY(fixture.visible(option.name));
            auto *box = fixture.window.readerWindow()->findChild<QCheckBox *>(option.name);
            QVERIFY(box);
            const bool previous = box->isChecked();
            QVERIFY(fixture.press(option.name));
            QTRY_COMPARE(box->isChecked(), !previous);
            expected[option.key] = !previous;
        }
        // Settings are a draft until the panel closes.
        QVERIFY(fixture.press("rrSettings"));
        QTRY_VERIFY(!fixture.visible("rrGeneralSettings"));
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        for (auto it = expected.begin(); it != expected.end(); ++it)
            QTRY_COMPARE(storage::readJson(fixture.paths.preferences()).value(it.key()), it.value());
        const auto saved = storage::readJson(fixture.paths.preferences());
        QVERIFY(fixture.press("rrSettings"));
        QVERIFY(fixture.press("rrSettingsControls"));
        QTRY_VERIFY(fixture.visible("rrShowLinkIndicator"));
        QCOMPARE(fixture.window.readerWindow()->findChild<QCheckBox *>("rrShowLinkIndicator")->isChecked(),
                 saved.value("show_link_indicator").toBool());
        QVERIFY(fixture.press("rrSettingsDefaults"));
        QTRY_VERIFY(fixture.visible("rrRestoreDefaults"));
        QVERIFY(fixture.press("rrRestoreDefaults"));
        QTRY_VERIFY(fixture.visible("rrRestoreConfirmation"));
        QVERIFY(fixture.press("rrKeepDefaults"));
        QTRY_VERIFY(!fixture.visible("rrRestoreConfirmation"));
        QCOMPARE(storage::readJson(fixture.paths.preferences()), saved);
        QVERIFY(fixture.press("rrSettingsControls"));
        QTRY_VERIFY(fixture.visible("rrShowLinkIndicator"));
        auto *indicator = fixture.window.readerWindow()->findChild<QCheckBox *>("rrShowLinkIndicator");
        const bool previous = indicator->isChecked();
        {
            BlockedFile blocked(fixture.paths.preferences());
            QVERIFY(blocked.active());
            QVERIFY(fixture.press("rrShowLinkIndicator"));
            QCOMPARE(indicator->isChecked(), !previous);
            QVERIFY(fixture.press("rrSettings"));
            QTRY_VERIFY(fixture.visible("rrSettingsWriteError"));
            QVERIFY(blocked.restore());
        }
        QCOMPARE(storage::readJson(fixture.paths.preferences()), saved);
        QVERIFY(fixture.press("rrSettingsWriteErrorConfirm"));
        QTRY_VERIFY(!fixture.visible("rrSettingsWriteError"));
        QTRY_VERIFY(fixture.visible("rrGeneralSettings"));
        QVERIFY(fixture.press("rrSettings"));
        QTRY_VERIFY(!fixture.visible("rrGeneralSettings"));
        QTRY_COMPARE(storage::readJson(fixture.paths.preferences()).value("show_link_indicator").toBool(), !previous);
    }
    void opticalSize() {
        Fixture fixture(false, true);
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrNavText"));
        QVERIFY(fixture.press("rrNavText"));
        QTRY_VERIFY(fixture.visible("rrFont"));
        QVERIFY(fixture.press("rrFont"));
        QWidget *choice = nullptr;
        for (int page = 0; page < 20 && !choice; ++page) {
            QCoreApplication::processEvents();
            for (auto *button : fixture.window.readerWindow()->findChildren<QPushButton *>())
                if (button->isVisible() && button->objectName().startsWith("rrFontChoice") &&
                    button->accessibleName().contains("Amstelvar", Qt::CaseInsensitive))
                    choice = button;
            if (!choice) {
                QVERIFY(fixture.widget("rrFontPagerNext")->isEnabled());
                QVERIFY(fixture.press("rrFontPagerNext"));
            }
        }
        QVERIFY(choice);
        QVERIFY(fixture.click(choice));
        QVERIFY(fixture.press("rrFontClose"));
        QTRY_VERIFY(fixture.visible("rrOpticalSizeMore"));
        QTRY_VERIFY(fixture.state().value("preview_ready").toBool());
        QTRY_VERIFY(fixture.state().value("preview_settings").toObject().value("font_face").toString().contains("Amstelvar"));
        QVERIFY(fixture.press("rrOpticalSizeMore"));
        QTRY_COMPARE(fixture.state().value("preview_settings").toObject().value("font_optical_size").toInt(), 15);
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QTRY_VERIFY(fixture.state().value("font_face").toString().contains("Amstelvar", Qt::CaseInsensitive));
        const auto selected = fixture.state();
        QCOMPARE(selected.value("font_optical_size").toInt(), 15); // Font declares a 14 pt default.
        QCOMPARE(selected.value("font_pixels").toInt(), defaultFontPixels());
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrReturn"));
        QVERIFY(fixture.press("rrReturn"));
        QTRY_VERIFY(fixture.window.idle());
        fixture.window.openBook(book);
        QTRY_COMPARE(opened.count(), 2);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QCOMPARE(fixture.state().value("font_optical_size"), selected.value("font_optical_size"));
        QCOMPARE(fixture.state().value("font_face"), selected.value("font_face"));
    }
    void desktopControls() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        fixture.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(2);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        auto *resolution = fixture.window.findChild<QComboBox *>("desktopResolutionPicker");
        auto *rotate = fixture.window.findChild<QAction *>("desktopRotateDevice");
        auto *toolbar = fixture.window.findChild<QAction *>("desktopShowToolbar");
        QVERIFY(resolution && rotate && toolbar);
        const auto original = fixture.state();
        for (int index : {0, 2, 1}) {
            resolution->setCurrentIndex(index);
            QTRY_VERIFY(!fixture.state().value("busy").toBool());
            QCOMPARE(fixture.state().value("position"), original.value("position"));
            QCOMPARE(fixture.state().value("pages"), original.value("pages"));
        }
        const auto portrait = fixture.window.readerWindow()->size();
        rotate->trigger();
        QTRY_COMPARE(fixture.window.readerWindow()->size(), portrait.transposed());
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QCOMPARE(opened.count(), 1); // Reflow reuses the open publication.
        rotate->trigger();
        QTRY_COMPARE(fixture.window.readerWindow()->size(), portrait);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QCOMPARE(fixture.state().value("position"), original.value("position"));
        toolbar->trigger();
        QTRY_VERIFY(!fixture.window.findChild<QWidget *>("desktopPreviewToolbar")->isVisible());
        toolbar->trigger();
        QTRY_VERIFY(fixture.window.findChild<QWidget *>("desktopPreviewToolbar")->isVisible());
        const int page = fixture.state().value("page").toInt();
        auto *next = fixture.window.findChild<QPushButton *>("desktopDeviceNext");
        QVERIFY(next && next->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_VERIFY(!fixture.state().value("busy").toBool() && fixture.state().value("page").toInt() > page);
        auto *previous = fixture.window.findChild<QPushButton *>("desktopDevicePrevious");
        QVERIFY(previous);
        QTest::mouseClick(previous, Qt::LeftButton);
        QTRY_VERIFY(!fixture.state().value("busy").toBool() && fixture.state().value("page").toInt() == page);
    }
    void navigation_data() { bookmarks_data(); }
    void navigation() {
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        openPrologueBody(fixture);
        const auto origin = fixture.state().value("position");
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrNavSeek"));
        QVERIFY(fixture.press("rrNavSeek"));
        QTRY_VERIFY(fixture.visible("rrBookPosition"));
        auto *scrubber = dynamic_cast<ui::PageScrubber *>(fixture.widget("rrPageScrubber"));
        QVERIFY(scrubber);
        QVERIFY(fixture.click(scrubber->slider(), {scrubber->slider()->width() * 3 / 4, scrubber->slider()->height() / 2}));
        QTRY_VERIFY(scrubber->previewReady());
        const int destination = scrubber->selectedPage();
        QVERIFY(destination != fixture.state().value("page").toInt());
        QCOMPARE(fixture.state().value("position"), origin);
        QVERIFY(fixture.press("rrNavSeek"));
        QTRY_VERIFY(!scrubber->selecting());
        QCOMPARE(fixture.state().value("position"), origin);
        QVERIFY(fixture.press("rrNavSeek"));
        QTRY_VERIFY(fixture.visible("rrBookPosition"));
        QVERIFY(fixture.click(scrubber->slider(), {scrubber->slider()->width() * 3 / 4, scrubber->slider()->height() / 2}));
        QTRY_VERIFY(scrubber->previewReady());
        const int accepted = scrubber->selectedPage();
        QVERIFY(fixture.click(scrubber, scrubber->previewBounds().center()));
        QTRY_VERIFY(!fixture.state().value("busy").toBool());
        QTRY_COMPARE(fixture.state().value("page").toInt(), accepted);
        QCOMPARE(opened.count(), 1);
    }
    void annotationTwoFingerTapConsumesWholeSequence() {
        ReadingPage page(nullptr, [] {});
        page.resize(600, 900);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto *device = QTest::createTouchDevice();
        auto touch = QTest::touchEvent(&page, device, false);
        const QPoint first(240, 420), second(340, 470);
        int requests = 0, taps = 0, holds = 0, swipes = 0, lightChanges = 0;
        page.annotationRequested = [&] {
            ++requests;
        };
        page.tapped = [&](QPoint) {
            ++taps;
        };
        page.held = [&](QPoint) {
            ++holds;
        };
        page.swiped = [&](int) {
            ++swipes;
        };
        page.lightChanged = [&](double) {
            ++lightChanges;
        };
        auto releaseBoth = [&] {
            touch.release(0, first, &page).release(1, second, &page).commit();
            QCoreApplication::processEvents();
        };
        touch.press(0, first, &page).press(1, second, &page).commit();
        releaseBoth();
        QCOMPARE(requests, 1);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        // Releasing one finger must not turn the remaining finger into a tap.
        touch.press(0, first, &page).commit();
        touch.move(0, first + QPoint(1, 0), &page).commit();
        touch.stationary(0).press(1, second, &page).commit();
        touch.release(1, second, &page).stationary(0).commit();
        touch.release(0, first, &page).commit();
        QCOMPARE(requests, 2);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        // A failed two-finger swipe is consumed, including its last release.
        touch.press(0, first, &page).press(1, second, &page).commit();
        touch.move(0, first + QPoint(180, 0), &page).stationary(1).commit();
        releaseBoth();
        QCOMPARE(requests, 2);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        // Three contacts and late second contacts never become shortcuts.
        touch.press(0, first, &page).press(1, second, &page).press(2, QPoint(300, 430), &page).commit();
        touch.release(0, first, &page).release(1, second, &page).release(2, QPoint(300, 430), &page).commit();
        touch.press(0, first, &page).commit();
        QTest::qWait(170);
        touch.stationary(0).press(1, second, &page).commit();
        releaseBoth();
        QCOMPARE(requests, 2);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        touch.press(0, first, &page).press(1, second, &page).commit();
        QTest::qWait(370);
        releaseBoth();
        QCOMPARE(requests, 2);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        touch.press(0, QPoint(60, 420), &page).press(1, second, &page).commit();
        touch.release(0, QPoint(60, 420), &page).release(1, second, &page).commit();
        touch.press(0, first, &page).press(1, second, &page).commit();
        QTouchEvent cancelled(QEvent::TouchCancel, device);
        QApplication::sendEvent(&page, &cancelled);
        releaseBoth();
        QCOMPARE(requests, 2);
        QCOMPARE(taps + holds + swipes + lightChanges, 0);
        // Single-finger reading remains available after failed gestures.
        touch.press(0, first, &page).commit();
        touch.release(0, first, &page).commit();
        QCOMPARE(taps, 1);
        QCOMPARE(holds + swipes + lightChanges, 0);
    }
    void annotationRightClickRejectsChords() {
        ReadingPage page(nullptr, [] {});
        page.resize(600, 900);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        int requests = 0;
        page.annotationRequested = [&] {
            ++requests;
        };
        const QPoint point(300, 450);
        QTest::mouseRelease(&page, Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(requests, 0);
        QTest::mousePress(&page, Qt::RightButton, Qt::NoModifier, point);
        QTest::mousePress(&page, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(&page, Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(requests, 0);
        QTest::mousePress(&page, Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseRelease(&page, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(&page, Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(requests, 0);
        QTest::mouseClick(&page, Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(requests, 1);
    }
    void stylesheet() {
        Fixture fixture;
        QVERIFY(fixture.prepared);
        // Isolate CSS reload from the publisher styles of the general book.
        QVERIFY(createBook(fixture.directory.path()));
        const auto styledBook = fixture.directory.path() + "/text.epub";
        const auto cssPath = fixture.paths.directory + "/userstyle.css";
        QFile css(cssPath);
        const QByteArray rules = "p { color: #ff0000 !important; }";
        QVERIFY(css.open(QIODevice::WriteOnly));
        QCOMPARE(css.write(rules), rules.size());
        css.close();
        const auto redPixels = [&] {
            const auto *page = static_cast<ReadingPage *>(fixture.widget("rrPage"));
            if (!page)
                return 0;
            const auto image = page->image.convertToFormat(QImage::Format_RGB32);
            int red = 0;
            for (int y = 0; y < image.height(); ++y) {
                const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x)
                    red += qRed(row[x]) > 180 && qGreen(row[x]) < 80 && qBlue(row[x]) < 80;
            }
            return red;
        };
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(styledBook);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        QVERIFY(redPixels() > 100);
        QVERIFY(QFile::remove(cssPath));
        QVERIFY(redPixels() > 100); // Changes apply on the next opening.
        QVERIFY(fixture.press("rrPage"));
        QTRY_VERIFY(fixture.visible("rrReturn"));
        QVERIFY(fixture.press("rrReturn"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.window.idle(), 30000);
        fixture.window.openBook(styledBook);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 2, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        QCOMPARE(redPixels(), 0);
    }
};

class ScreenshotTests final : public QObject {
    Q_OBJECT
private slots:
    void screens_data() { variants(); }
    void screens() {
        QFETCH(int, preset);
        QFETCH(bool, dark);
        Fixture fixture(dark);
        QVERIFY(fixture.prepared);
        fixture.window.findChild<QComboBox *>("desktopScreenPicker")->setCurrentIndex(preset);
        const auto device = QStringList{"fill", "six-inch", "seven-inch"}.at(preset);
        const auto name = device + (dark ? "-dark" : "-light");
        if (!dark) {
            QVERIFY(capture(fixture, device + "-startup"));
        }
        QSignalSpy opened(&fixture.window, &desktop::Window::bookOpened);
        fixture.window.openBook(book);
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.state().value("busy").toBool(), 30000);
        auto *reader = fixture.window.readerWindow();
        QVERIFY(reader);
        QCOMPARE(reader->property("reader_dark").toBool(), dark);
        QVERIFY(hasText(reader->findChild<QWidget *>("rrPage")->grab().toImage()));
        QVERIFY(capture(fixture, name + "-cover"));
        openPrologueBody(fixture);
        QVERIFY(capture(fixture, name + "-reading"));
        tour(fixture, name, true);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts:freetype");
    FontCopies fontCopies;
    copies = &fontCopies;
    QApplication app(argc, argv);
    if (QGuiApplication::platformName() != "minimal") {
        qCritical() << "This executable only runs headlessly.";
        return 2;
    }
    app.setApplicationName("Kobalt headless tests");
    app.setStyle("Fusion");
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir fixtureDirectory(QDir::tempPath() + "/kobalt-book-XXXXXX");
    AssetBundle bundle(QStringLiteral(KOBALT_TEST_ASSETS), QStringLiteral(KOBALT_VERSION));
    assets = &bundle;
    if (!fixtureDirectory.isValid() || !bundle.available() ||
        !ui::initializeInterfaceFonts(bundle)) {
        qCritical() << "Cannot prepare the test book or runtime assets.";
        return 2;
    }
    for (const auto &path : bundledFontFiles(bundle)) {
        if (QFontDatabase::addApplicationFont(path) < 0) {
            qCritical() << "Cannot register bundled font:" << path;
            return 2;
        }
    }
    readingPpi.store(300);
    ui::setDensity(300);
    app.setFont(ui::interfaceFont());
    auto arguments = app.arguments();
    if (arguments.size() == 3 && arguments.at(1) == "--regression") {
        // Share the offline EPUB fixture with the restored standalone checks.
        const auto name = QFileInfo(arguments.at(2)).fileName();
        const bool lookup = name == "kobalt-search-tests" || name == "kobalt-word-selection-tests";
        const bool links = name == "kobalt-links-tests";
        QMap<QString, test::BookFiles> engineBooks;
        QStringList inputs{fixtureDirectory.path() + "/text.epub"};
        if (name == "kobalt-document-cache-tests") {
            engineBooks = {{"text.epub", test::engineBook()}};
        } else if (name == "kobalt-reader-tests") {
            engineBooks = {{"text.epub", test::engineBook()}, {"color.epub", test::engineBook(true)}};
            inputs.append(fixtureDirectory.path() + "/color.epub");
        } else if (name == "kobalt-local-reader-tests") {
            engineBooks = {{"text.epub", test::engineBook(false, 2, 300)}, {"color.epub", test::engineBook(true)}};
            inputs.append(fixtureDirectory.path() + "/color.epub");
        } else if (name == "kobalt-document-word-count-tests") {
            engineBooks = {{"text.epub", test::wordCountBook()}, {"word-count-many.epub", test::wordCountBook(1500)}};
            inputs.append(fixtureDirectory.path() + "/word-count-many.epub");
        } else if (name == "kobalt-publisher-markup-tests") {
            QFile font(bundle.file("fonts/IBMPlexSans-Regular.ttf"));
            if (!font.open(QIODevice::ReadOnly))
                return 2;
            engineBooks = test::publisherBooks(font.readAll());
            inputs = {fixtureDirectory.path() + "/publisher.epub", fixtureDirectory.path() + "/publisher-de.epub",
                      fixtureDirectory.path() + "/publisher-ru.epub"};
        } else if (!(lookup || links ? writeBook(fixtureDirectory.path(), "text.epub", links ? test::linkBook() : test::lookupBook())
                                     : createBook(fixtureDirectory.path()))) {
            return 2;
        }
        for (auto it = engineBooks.cbegin(); it != engineBooks.cend(); ++it)
            if (it.value().isEmpty() || !writeBook(fixtureDirectory.path(), it.key(), it.value()))
                return 2;
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        if (QFileInfo(arguments.at(2)).fileName() == "kobalt-reader-session-tests") {
            for (const auto *name : {"formatting-unsupported-writing.epub", "later-images.epub", "publisher-non-linear.epub"})
                if (!createBook(fixtureDirectory.path(), name))
                    return 2;
            inputs.append(fixtureDirectory.path() + "/publisher-non-linear.epub");
        }
        child.start(arguments.at(2), inputs);
        if (!child.waitForFinished(20000)) {
            const auto error = child.errorString();
            child.kill();
            child.waitForFinished();
            qCritical() << "Regression process did not finish:" << error;
            return 2;
        }
        return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : 2;
    }
    const bool screenshots = arguments.size() >= 2 && arguments.at(1) == "--screenshots";
    QString output;
    std::unique_ptr<QTemporaryDir> staging;
    if (screenshots) {
        if (arguments.size() != 3 || !QDir::isAbsolutePath(arguments.at(2))) {
            qCritical() << "Usage: kobalt-desktop-tests --screenshots ABSOLUTE_DIRECTORY";
            return 2;
        }
        output = QDir::cleanPath(arguments.at(2));
        if (QFileInfo::exists(output)) {
            QFile previous(output + "/manifest.json");
            if (!previous.open(QIODevice::ReadOnly) ||
                QJsonDocument::fromJson(previous.readAll()).object().value("generator") != "kobalt-desktop") {
                qCritical() << "Refusing to replace a directory not created by this screenshot generator:" << output;
                return 2;
            }
        }
        const auto parent = QFileInfo(output).absolutePath();
        if (!QDir().mkpath(parent)) {
            return 2;
        }
        // The CMake parent owns TMPDIR and can clean captures after a timeout.
        staging = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/kobalt-captures-XXXXXX");
        captureDirectory = staging->path() + "/images";
        if (!staging->isValid() || !QDir().mkpath(captureDirectory)) {
            return 2;
        }
        arguments = {arguments.first()};
    }
    DesktopTests tests;
    ScreenshotTests screens;
    QTimer::singleShot(0, &app, [&] {
        app.exit(QTest::qExec(screenshots ? static_cast<QObject *>(&screens) : &tests, arguments));
    });
    const int result = app.exec();
    if (result != 0 || !screenshots) {
        return result;
    }
    QFile fixture(book);
    if (!fixture.open(QIODevice::ReadOnly) || captures.size() != 133) {
        return 2;
    }
    const QJsonObject manifest{{"schema", 1}, {"generator", "kobalt-desktop"}, {"qt", qVersion()}, {"version", KOBALT_VERSION},
                               {"commit", kobaltCommit}, {"modified", bool(KOBALT_COMMIT_MODIFIED)},
                               {"book", "Trevelyan's Type Tester v1.2 with chapter-heading CSS fix"}, {"book_file", QFileInfo(book).fileName()},
                               {"book_sha256", QString::fromLatin1(QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256).toHex())},
                               {"images", captures}};
    QSaveFile record(captureDirectory + "/manifest.json");
    const auto json = QJsonDocument(manifest).toJson();
    if (!record.open(QIODevice::WriteOnly) || record.write(json) != json.size() || !record.commit()) {
        return 2;
    }
    QString gallery = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Kobalt screenshots</title>
<style>body{font:16px system-ui;margin:24px;background:#eee;color:#222}header{position:sticky;top:0;background:#eee;padding:12px 0}input{font:inherit;padding:8px;width:320px;max-width:90%}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:20px}figure{margin:0;background:white;padding:12px;border:1px solid #ccc}figure[hidden]{display:none}img{width:100%;height:380px;object-fit:contain;background:#ddd}figcaption{padding-top:8px;overflow-wrap:anywhere}a{color:inherit}</style>
<header><h1>Kobalt screenshots</h1><p>Actual native app views. Open an image for full size. <a href="manifest.json">Checksums and build details</a></p>
<input aria-label="Filter screenshots" placeholder="Filter: six-inch, dark, search…" oninput="for(const f of document.querySelectorAll('figure')) f.hidden=!f.textContent.toLowerCase().includes(this.value.toLowerCase())"></header><main>
)HTML";
    for (const auto &item : captures) {
        const auto entry = item.toObject();
        const auto file = entry.value("file").toString().toHtmlEscaped();
        gallery += QString("<figure><a href=\"%1\"><img loading=\"lazy\" src=\"%1\" alt=\"%1\"></a>"
                           "<figcaption>%1 · %2 × %3</figcaption></figure>\n")
                       .arg(file).arg(entry.value("width").toInt()).arg(entry.value("height").toInt());
    }
    gallery += "</main></html>\n";
    QSaveFile index(captureDirectory + "/index.html");
    const auto html = gallery.toUtf8();
    if (!index.open(QIODevice::WriteOnly) || index.write(html) != html.size() || !index.commit()) {
        return 2;
    }
    // Keep the last complete set outside TMPDIR until replacement succeeds.
    const auto previous = QFileInfo(output).absolutePath() + '/' + QFileInfo(staging->path()).fileName() + "-previous";
    const bool existing = QFileInfo::exists(output);
    if (existing && !QDir().rename(output, previous)) {
        return 2;
    }
    if (!QDir().rename(captureDirectory, output)) {
        if (existing && !QDir().rename(previous, output)) {
            qCritical() << "Previous screenshots retained at" << previous;
        }
        return 2;
    }
    if (existing && !QDir(previous).removeRecursively())
        qWarning() << "Cannot remove superseded screenshots:" << previous;
    qInfo().noquote() << "Saved" << captures.size() << "screenshots and manifest.json to" << output;
    return 0;
}

#include "desktop.moc"
