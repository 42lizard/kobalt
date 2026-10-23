#include "interfaces/reader/ui/controls/page_scrubber.h"
#include "interfaces/reader/ui/controls/icons.h"
#include "interfaces/reader/ui/dialogs/modals.h"
#include "interfaces/reader/ui/style/theme.h"
#include "interfaces/reader/panels/dictionary_panel.h"
#include "interfaces/reader/settings/acknowledgments.h"
#include "interfaces/reader/panels/chapter_graph.h"
#include "interfaces/reader/ui/controls/paged_text.h"
#include "interfaces/reader/panels/search_panel.h"
#include "interfaces/reader/ui/style/styles.h"
#include "host_services.h"
#include "reader_host.h"
#include "reading/appearance_settings.h"
#include "host_presentation.h"
#include "platform/android/start_screen.h"
#include "interfaces/reader/views/opening.h"
#include "dictionaries/manager.h"
#include "dictionaries/stardict.h"
#include "reader_canvas.h"
#include "host_platform.h"
#include "host_style.h"
#include "host_fixture.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/views/reading_page.h"
#include "interfaces/reader/views/sleep_screen.h"
#include "interfaces/reader/settings/dictionaries_page.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "reading/reading_density.h"
#include "storage/reader_storage.h"
#include "storage/highlight_store.h"
#include "support/strings.h"
#include <QApplication>
#include <QCheckBox>
#include <QBuffer>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>
#include <cstdio>
#include <stdexcept>

using namespace kobalt;
namespace {
void require(bool ok, const QString &message) {
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
void waitFor(const std::function<bool()> &done, const QString &message, int timeout = 30000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }
    require(done(), message);
}
struct LookupState {
    QString mode = "definition", word, selected;
    int selections = 0, lookups = 0;
};
class ReferenceDictionary final : public DictionaryService {
public:
    explicit ReferenceDictionary(std::shared_ptr<LookupState> state) : state(std::move(state)) {}
    QVector<DictionaryChoice> choices() const override {
        return {{"en-en", "English monolingual"}};
    }
    bool select(const QString &, const QString &id, QString *) override {
        state->selected = id;
        ++state->selections;
        return id == "en-en";
    }
    DictionaryResult lookup(const QString &, const QString &word) override {
        state->word = word;
        ++state->lookups;
        DictionaryResult result;
        result.word = word;
        result.received = true;
        result.restored = true;
        result.dictionaryId = "en-en";
        result.dictionary = "English monolingual";
        if (state->mode == "missing")
            return result;
        if (state->mode == "unavailable") {
            result.received = false;
            result.error = "The dictionary service is unavailable.";
            return result;
        }
        if (!ready) {
            const auto archive = qEnvironmentVariable("KOBALT_SCREENSHOT_DICTIONARY");
            require(!archive.isEmpty(), "The screenshot requires a real reference dictionary archive");
            QString error;
            for (const auto &entry : DictionaryManager::catalog())
                if (entry.id == "en-en")
                    require(DictionaryManager::installArchive(directory.path(), entry, archive, &error), error);
            require(dictionary.open(directory.path() + "/en-en", "en-en", &error), error);
            ready = true;
        }
        result = dictionary.lookup(word);
        result.dictionaryId = "en-en";
        result.dictionary = "English monolingual";
        result.source = "Princeton WordNet and Wiktionary · koreader-dicts v1.2.0 · CC BY-SA 4.0";
        return result;
    }

private:
    std::shared_ptr<LookupState> state;
    QTemporaryDir directory;
    StarDict dictionary;
    bool ready = false;
};

class Tour {
public:
    Tour(const QString &book, const QString &output, const QString &group, const QString &theme,
         const EngineFontInputs &fonts, const QString &placeholder, QSize size, const QString &fixtureDate)
        : output(output), group(group), theme(theme), bookPath(book), fixtureDate(fixtureDate), size(size),
          paths{temporary.path()}, host(paths, fonts, placeholder), lookup(std::make_shared<LookupState>()) {
        try {
            require(temporary.isValid(), "Cannot create isolated reader state");
            QDir().mkpath(output);
            require(storage::writeJson(paths.preferences(),
                                       {{"dark_mode", theme == "dark"},
                                        {"dense_stats_chart", group == "stats-dense"},
                                        {"page_navigation", group == "light" ? "buttons_only" : "taps_and_swipes"},
                                        {"show_navigation_bar", group != "links-legacy"}}),
                    "Cannot write fixture preferences");
            if (group == "layout-saved-field") {
                auto preferences = storage::readJson(paths.preferences());
                auto custom = headerfooter::presetLayout(headerfooter::Preset::Default);
                custom.slotFields[3] = headerfooter::Field::ChapterTimeLeft;
                preferences[headerfooter::customKey] = headerfooter::toJson(custom);
                preferences[appearance::lastPresetKey] = headerfooter::presetValue(headerfooter::Preset::Custom);
                require(storage::writeJson(paths.preferences(), preferences), "Cannot save Custom field fixture");
            }
            if (group == "sync") {
                host.progress = std::make_shared<test::HostProgress>();
                host.positions = {[](const ReaderModel &, const QString &position, QString *) {
                                      return position;
                                  },
                                  [](const ReaderModel &model, QString *) {
                                      return model.position();
                                  },
                                  [](const QString &position) {
                                      return position;
                                  }};
                host.progress->current = {
                    QUrl::fromLocalFile(QFileInfo(book).absoluteFilePath()).toString(), {}, 100, 1, true};
            }
            host.configureServices = [this, state = lookup, desktop = group == "native-ui",
                                      catalogue = discoverReadingFonts(fonts.sources)](ReaderHostServices &services) {
                services.desktopUi = desktop;
                services.colorDisplay = [] {
                    return qEnvironmentVariableIsEmpty("KOBALT_MONO_DISPLAY");
                };
                services.pageButtons = [this] {
                    return pageButtonsSupported;
                };
                if (!desktop) {
                    auto automatic = std::make_shared<bool>(true);
                    services.automaticBookOpening = [automatic] {
                        return *automatic;
                    };
                    services.setAutomaticBookOpening = [automatic](bool enabled) {
                        *automatic = enabled;
                        return true;
                    };
                    services.presentation = [](QWidget *) {
                        return std::make_unique<test::HostPresentation>(
                            std::make_shared<test::SleepPresentationState>());
                    };
                }
                services.systemRotation = this->group == "layout-system-rotation";
                if (this->group == "light-sensor") {
                    services.light = test::hostReaderServices(true).light;
                }
                if (this->group == "bluetooth")
                    services.observeBluetooth = [this](QObject *, std::function<void(bool)> changed) {
                        bluetoothChanged = std::move(changed);
                    };
                if (desktop) {
                    services.lightAvailable = [] {
                        return false;
                    };
                    services.animationsAvailable = [] {
                        return false;
                    };
                }
                services.fontFamilies = [catalogue] {
                    QStringList families;
                    for (const auto &font : catalogue)
                        if (font.origin == FontOrigin::BuiltIn)
                            families.append(font.family);
                    return families;
                };
                services.dictionary = [state] {
                    return std::make_unique<ReferenceDictionary>(state);
                };
                services.browserAvailable = [] {
                    return true;
                };
                services.openBrowser = [](const QUrl &, QWidget *) {
                    return true;
                };
            };
            QSignalSpy opened(&host, &test::ReaderHost::opened), failed(&host, &test::ReaderHost::failed);
            host.open(book, size);
            waitFor(
                [&] {
                    return !opened.isEmpty() || !failed.isEmpty();
                },
                "Book opening timed out", 60000);
            require(failed.isEmpty(), failed.isEmpty() ? QString() : failed.first().first().toString());
            canvas = std::make_unique<test::ReaderCanvas>(host.window(), size);
            canvas->resize(size);
            canvas->show();
            settled();
        } catch (...) {
            close();
            throw;
        }
    }
    void close() noexcept {
        host.shutdown();
        try {
            waitFor(
                [&] {
                    return host.idle();
                },
                "Reader did not close");
        } catch (const std::exception &error) {
            qFatal("%s", error.what());
        }
    }
    void reloadCreatedFixtures() {
        close();
        canvas.reset();
        int stores = 0;
        QDirIterator files(paths.annotations(), {"bookmarks.json", "highlights.json"}, QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            const auto saved = storage::readJsonFile(file, 1024 * 1024);
            require(saved.status == storage::ReadStatus::Ok, "Creation-date fixture store is unreadable");
            auto object = saved.object;
            const QString key = QFileInfo(file).fileName() == "bookmarks.json" ? "bookmarks" : "highlights";
            require(object.value(key).isArray(), "Creation-date fixture has no entry array");
            const auto original = object.value(key).toArray();
            require(!original.isEmpty(), "Creation-date fixture has no entries");
            QJsonArray entries;
            for (const auto &value : original) {
                require(value.isObject(), "Creation-date fixture entry is not an object");
                const auto before = value.toObject();
                require(QDateTime::fromString(before.value("created").toString(), Qt::ISODate).isValid(),
                        "Creation-date fixture entry has an invalid date");
                auto after = before;
                after["created"] = fixtureDate;
                auto beforeFields = before, afterFields = after;
                beforeFields.remove("created");
                afterFields.remove("created");
                require(beforeFields == afterFields, "Creation-date fixture changed entry fields");
                entries.append(after);
            }
            require(entries.size() == original.size(), "Creation-date fixture changed entry count");
            object[key] = entries;
            auto beforeFields = saved.object, afterFields = object;
            beforeFields.remove(key);
            afterFields.remove(key);
            require(beforeFields == afterFields, "Creation-date fixture changed store metadata");
            require(storage::writeJson(file, object), "Cannot save creation-date fixture");
            const auto written = storage::readJsonFile(file, 1024 * 1024);
            require(written.status == storage::ReadStatus::Ok && written.object == object,
                    "Creation-date fixture did not round trip");
            ++stores;
        }
        require(stores > 0, "Creation-date fixture did not find a saved store");
        QSignalSpy opened(&host, &test::ReaderHost::opened), failed(&host, &test::ReaderHost::failed);
        host.open(bookPath, size);
        waitFor(
            [&] {
                return !opened.isEmpty() || !failed.isEmpty();
            },
            "Fixture reopen timed out", 60000);
        require(failed.isEmpty(), "Creation-date fixture could not reopen the book");
        canvas = std::make_unique<test::ReaderCanvas>(host.window(), size);
        canvas->resize(size);
        canvas->show();
        settled();
        createdFixturesReloaded = true;
    }
    ~Tour() {
        close();
    }
    QWidget *widget(const QString &name) const {
        auto *result = host.window()->findChild<QWidget *>(name);
        require(result, "Missing widget: " + name);
        return result;
    }
    bool visible(const QString &name) const {
        auto *result = host.window()->findChild<QWidget *>(name);
        return result && result->isVisible();
    }
    void settled() {
        QCoreApplication::processEvents();
        waitFor(
            [&] {
                auto *settings = host.window()->findChild<QWidget *>("rrSettings");
                return !host.session()->busy() && (!settings || settings->isEnabled());
            },
            "Renderer busy", 60000);
        QCoreApplication::processEvents();
    }
    void tap(const QString &name) {
        QPointer<QWidget> target;
        // A preview refresh can replace a font choice while input is waiting.
        waitFor(
            [&] {
                target = host.window()->findChild<QWidget *>(name);
                return target && target->isVisible() && target->isEnabled();
            },
            "Unavailable control: " + name);
        auto *pages = host.window()->findChild<QWidget *>("rrSettingsPages");
        if (pages && pages->isAncestorOf(target))
            revealControlOption(name);
        clickAt(target->mapTo(canvas->surface(), target->rect().center()), target);
        settled();
    }
    void clickAt(QPoint point, QWidget *expected = nullptr) {
        canvas->ensureVisible(point.x(), point.y(), 30, 30);
        QCoreApplication::processEvents();
        const QPoint local = canvas->surface()->mapTo(canvas->viewport(), point);
        require(canvas->viewport()->rect().contains(local), "Input point is clipped by the canvas viewport");
        auto *hit = canvas->viewport()->childAt(local);
        require(hit && (!expected || hit == expected || expected->isAncestorOf(hit)),
                "Input point is intercepted by another control or overlay");
        require(canvas->windowHandle(), "Canvas input window is unavailable");
        QTest::mouseClick(canvas->windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          canvas->surface()->mapTo(canvas.get(), point));
    }
    void revealControlOption(const QString &name) {
        auto *option = widget(name);
        require(option->isVisible(), "Controls option is hidden: " + name);
        auto *pages = host.window()->findChild<QWidget *>("rrSettingsPages");
        auto *clip = host.window()->findChild<QWidget *>("rrSettingsPageClip");
        if (pages && clip && pages->isAncestorOf(option)) {
            auto *previous = qobject_cast<QPushButton *>(widget("rrSettingsOptionsPrevious"));
            auto *next = qobject_cast<QPushButton *>(widget("rrSettingsOptionsNext"));
            const auto contains = [&] {
                return clip->rect().contains(option->mapTo(clip, option->rect().center()));
            };
            while (!contains() && previous->isVisible() && previous->isEnabled()) {
                clickAt(previous->mapTo(canvas->surface(), previous->rect().center()), previous);
                settled();
            }
            for (int turn = 0; !contains() && next->isVisible() && next->isEnabled() && turn < 100; ++turn) {
                clickAt(next->mapTo(canvas->surface(), next->rect().center()), next);
                settled();
            }
            require(contains(), "Settings option is not reachable: " + name);
        }
        settled();
    }
    bool lastListPage(const QString &prefix) {
        auto *counter = qobject_cast<QLabel *>(widget(prefix + "Page"));
        require(counter, "List page counter is missing");
        const auto parts = counter->text().split(" of ");
        require(parts.size() == 2, "List page counter has an unexpected format");
        return parts.first() == parts.last();
    }
    int checkPagerEnds(const QString &prefix, QWidget *content = nullptr) {
        auto *next = widget(prefix + "Next");
        auto *previous = widget(prefix + "Previous");
        auto *counter = qobject_cast<QLabel *>(widget(prefix + "Page"));
        require(counter, "Pager counter is not a label");
        const QString initial = counter->text();
        const auto firstContent = content ? content->grab().toImage() : QImage();
        int turns = 0;
        while (next->isVisible() && next->isEnabled() && !lastListPage(prefix)) {
            require(turns < 128, "Pager did not reach its end within 128 pages");
            const QString before = counter->text();
            tap(prefix + "Next");
            require(counter->text() != before, "Next did not advance the page counter");
            require(previous->isVisible() && previous->isEnabled(), "Previous unavailable after advancing");
            ++turns;
        }
        require(turns > 0, "Long-content fixture has only one reachable page");
        if (content) {
            require(content->isVisible() && content->grab().toImage() != firstContent,
                    "Last page did not expose different content");
            const QRect bounds(content->mapTo(canvas->surface(), QPoint()), content->size());
            require(canvas->surface()->rect().contains(bounds), "Paged content extends beyond the reader surface");
        }
        if (auto *license = dynamic_cast<LicenseText *>(content))
            require(license->page == license->pages() - 1, "License pager did not reach the final content page");
        if (auto *graph = dynamic_cast<ChapterGraph *>(content))
            require(graph->graphPage() == graph->graphPages() - 1, "Graph pager did not reach the final chapters");
        for (int n = 0; n < turns; ++n) {
            const QString before = counter->text();
            tap(prefix + "Previous");
            require(counter->text() != before, "Previous did not restore an earlier page");
        }
        require(counter->text() == initial, "Pager did not restore its first page");
        return turns;
    }
    void pageTap() {
        auto *page = widget("rrPage");
        clickAt(page->mapTo(canvas->surface(), QPoint(size.width() / 2, size.height() / 2)));
        settled();
    }
    void menu() {
        if (!visible("rrControlsTop"))
            pageTap();
        require(visible("rrControlsTop"), "Reading menu did not open");
    }
    void capture(const QString &name, const QString &required = "rrPage", QWidget *surface = nullptr,
                 qreal surfaceDpr = 2) {
        if (!qEnvironmentVariable("KOBALT_SCREENSHOT_ONLY").isEmpty() &&
            !qEnvironmentVariable("KOBALT_SCREENSHOT_ONLY").split(',').contains(name))
            return;
        settled();
        waitFor(
            [&] {
                return surface ? (surface->findChild<QWidget *>(required) &&
                                  surface->findChild<QWidget *>(required)->isVisible())
                               : visible(required);
            },
            "Capture target is hidden: " + required);
        QImage previous, image;
        int stable = 0;
        QElapsedTimer timeout;
        timeout.start();
        while (stable < 3 && timeout.elapsed() < 10000) {
            QCoreApplication::processEvents();
            if (surface) {
                QPixmap frame(size);
                frame.setDevicePixelRatio(surfaceDpr);
                frame.fill(Qt::white);
                surface->render(&frame);
                image = frame.toImage();
                image.setDevicePixelRatio(1);
            } else
                image = canvas->capture();
            stable = image == previous ? stable + 1 : 0;
            previous = image;
            QTest::qWait(20);
        }
        require(stable == 3 && !image.isNull(), "Image did not settle: " + name);
        auto *target = (surface ? surface : host.window())->findChild<QWidget *>(required);
        require(target && target->isVisible(), "Capture target disappeared: " + required);
        const QString filename = name + ".png";
        require(image.save(output + "/" + filename), "Could not save " + filename);
        QJsonArray widgets;
        auto *root = surface ? surface : host.window();
        for (auto *item : root->findChildren<QWidget *>()) {
            if (!item->isVisible() || item->objectName().isEmpty())
                continue;
            const auto point = item->mapTo(root, QPoint{});
            QJsonObject value{{"name", item->objectName()}, {"x", point.x()},           {"y", point.y()},
                              {"width", item->width()},     {"height", item->height()}, {"enabled", item->isEnabled()}};
            if (auto *label = qobject_cast<QLabel *>(item))
                value["text"] = label->text();
            if (auto *button = qobject_cast<QAbstractButton *>(item))
                value["text"] = button->text();
            widgets.append(value);
        }
        QFile assetManifest(QStringLiteral(KOBALT_ASSET_ROOT) + "/manifest.json");
        require(assetManifest.open(QIODevice::ReadOnly), "Asset manifest unavailable");
        const auto assetsHash =
            QString::fromLatin1(QCryptographicHash::hash(assetManifest.readAll(), QCryptographicHash::Sha256).toHex());
        QJsonObject metadata{{"assets_sha256", assetsHash},
                             {"group", group},
                             {"theme", theme},
                             {"name", name},
                             {"file", filename},
                             {"width", size.width()},
                             {"height", size.height()},
                             {"clock", "09:00"},
                             {"fixture_date", fixtureDate},
                             {"created_fixtures_reloaded", createdFixturesReloaded},
                             {"battery_percent", 100},
                             {"required_widget", required},
                             {"selected_word", lookup->word},
                             {"page", host.session()->page() + 1},
                             {"chapter", host.session()->chapterTitle()},
                             {"chapter_count", host.session()->chapters().size()},
                             {"widgets", widgets},
                             {"device_fixture", test::fixtureProvenance()},
                             {"qt", qVersion()},
                             {"platform", QGuiApplication::platformName()},
                             {"ppi", ui::density()},
                             {"reading_ppi", readingPpi.load()},
                             {"dpr", image.devicePixelRatio()},
                             {"style", test::hostStyleProfile},
                             {"font_profile", test::hostFontProvenance()}};
        if (group == "sleep" && required == "rrSleepPreview") {
            auto *preview = host.window()->findChild<QWidget *>("rrSleepPreview");
            require(preview, "Sleep preview unavailable");
            metadata["battery_percent"] = preview->property("sleep_battery").toInt();
            metadata["sleep_status"] = preview->property("sleep_status").toString();
            metadata["sleep_background"] = preview->property("sleep_background").toString();
        }
        if (group == "dictionary")
            metadata["dictionary_reference"] = "koreader-dicts v1.2.0/en-en";
        if (group == "dictionary" && lookup->mode != "definition")
            metadata["dictionary_fixture"] = "emulated-error-state";
        if (dictionaryManagementFixture)
            metadata["dictionary_fixture"] = "emulated-management-state";
        require(storage::writeJson(output + "/" + name + ".json", metadata), "Could not save metadata");
        std::printf("CAPTURE %s/%s/%s\n", theme.toUtf8().constData(), group.toUtf8().constData(),
                    name.toUtf8().constData());
        std::fflush(stdout);
    }
    SearchHit find(const QString &text) {
        struct Result {
            QVector<SearchHit> hits;
            bool done = false;
        };
        auto result = std::make_shared<Result>();
        host.session()->search(text, [result](SearchBatch batch) {
            result->hits += batch.hits;
            result->done = batch.done;
        });
        waitFor(
            [&] {
                return result->done;
            },
            "Search did not finish: " + text, 60000);
        require(!result->hits.isEmpty(), "Fixture text not found: " + text);
        const auto hit = result->hits.first();
        require(host.session()->goTo(hit.start), "Cannot go to fixture text");
        settled();
        return hit;
    }
    QPoint selection(const SearchHit &hit) {
        struct Result {
            QVector<QRect> rects;
            bool done = false;
        };
        auto result = std::make_shared<Result>();
        host.session()->rangeRects(hit.start, hit.end, [result](QVector<QRect> rects) {
            result->rects = rects;
            result->done = true;
        });
        waitFor(
            [&] {
                return result->done;
            },
            "Selection rectangles unavailable");
        require(!result->rects.isEmpty(), "Empty selection");
        return result->rects.first().center();
    }
    void hold(QPoint point) {
        auto *page = host.window()->findChild<ReadingPage *>("rrPage");
        point += QPoint(0, page->topHeight());
        QTest::mousePress(page, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(ui::metrics::lookup::holdMilliseconds + 50);
        QTest::mouseRelease(page, Qt::LeftButton, Qt::NoModifier, point);
    }
    void dictionary(const QString &word) {
        const auto hit = find(word);
        const auto at = selection(hit);
        capture("selection-page");
        auto selected = std::make_shared<WordSelection>();
        auto done = std::make_shared<bool>(false);
        host.session()->wordAt(at, [selected, done](WordSelection value) {
            *selected = std::move(value);
            *done = true;
        });
        waitFor(
            [&] {
                return *done;
            },
            "Word selection timed out");
        require(!selected->text.isEmpty(),
                QString("No word at fixture point %1,%2 for %3").arg(at.x()).arg(at.y()).arg(word));
        hold(at);
        waitFor(
            [&] {
                return visible("rrDictionary");
            },
            "Dictionary did not open");
        capture("definition", "rrDictionary");
        auto *panel = dynamic_cast<DictionaryPanel *>(widget("rrDictionary"));
        require(panel, "Dictionary lookup panel missing");
        panel->setLanguage("en");
        panel->setChoices(
            {{"en-en", "English monolingual", "en", true},
             {"custom:/mnt/onboard/.kobo/custom-dict/dicthtml-en.zip", "English monolingual (Custom)", "en", true},
             {"downloaded:en-fr", "English to French", "en", false},
             {"downloaded:ar-ar", "Arabic monolingual", "ar", false}});
        tap("rrDictionarySelect");
        capture("lookup-choices", "rrDictionaryChoices");
        const int selections = lookup->selections, lookups = lookup->lookups;
        QString currentChoice;
        for (auto *button : panel->findChildren<QPushButton *>())
            if (button->objectName().startsWith("rrDictionaryChoice") && button->isChecked())
                currentChoice = button->objectName();
        require(!currentChoice.isEmpty(), "Current dictionary choice is missing");
        tap(currentChoice);
        // DictionaryManager selects the fallback in select(), then again in
        // lookup() before invoking the real reference dictionary.
        require(lookup->selections == selections + 2 && lookup->selected == "en-en" && lookup->lookups == lookups + 1,
                QString("Dictionary choice callback counts: selections %1 -> %2, lookups %3 -> %4, selected %5")
                    .arg(selections)
                    .arg(lookup->selections)
                    .arg(lookups)
                    .arg(lookup->lookups)
                    .arg(lookup->selected));
        require(!visible("rrDictionaryChoices"), "Dictionary choices stayed open after selection");
        tap("rrDictionaryClose");
        const auto longHit = find("set");
        const auto longAt = selection(longHit);
        hold(longAt);
        waitFor(
            [&] {
                return visible("rrDictionary");
            },
            "Long reference entry did not open");
        capture("long-definition", "rrDictionary");
        auto *definition = dynamic_cast<ui::PagedText *>(widget("rrDefinition"));
        require(definition && definition->pages() > 1 && definition->page() == 0, "Long definition did not paginate");
        const auto firstDefinition = definition->grab().toImage();
        tap("rrDictionaryNext");
        capture("definition-page-2", "rrDictionary");
        require(definition->page() == 1 && definition->grab().toImage() != firstDefinition,
                "Next did not change the definition page and rendered content");
        tap("rrDictionaryPrevious");
        require(definition->page() == 0 && definition->grab().toImage() == firstDefinition,
                "Previous did not restore the first definition page");
        tap("rrDictionaryClose");
        lookup->mode = "missing";
        hold(longAt);
        waitFor(
            [&] {
                return visible("rrDictionary");
            },
            "Missing definition did not open");
        capture("no-definition", "rrDictionary");
        tap("rrDictionaryClose");
        lookup->mode = "unavailable";
        hold(longAt);
        waitFor(
            [&] {
                return visible("rrDictionary");
            },
            "Dictionary error did not open");
        capture("unavailable", "rrDictionary");
    }
    void supplementalFonts() {
        if (supplementalFontFamily(host.session()->state()["language"].toString()).isEmpty())
            return;
        pageTap();
        menu();
        tap("rrNavText");
        tap("rrFont");
        for (int turn = 0; !lastListPage("rrFontPager"); ++turn) {
            require(turn < 128, "Font pager did not reach its final page");
            tap("rrFontPagerNext");
        }
        auto *section = qobject_cast<QLabel *>(widget("rrFontSection"));
        require(section && section->text() == "SUPPLEMENTAL FONTS", "Language font section did not appear");
        bool matchingFont = false;
        const auto family = supplementalFontFamily(host.session()->state()["language"].toString());
        for (auto *label : host.window()->findChildren<QLabel *>())
            if (label->isVisible() && label->objectName().endsWith("Title") && label->text() == family)
                matchingFont = true;
        require(matchingFont, "Supplemental page does not offer the document's font: " + family);
        capture("supplemental-fonts", "rrChoices");
    }
    void run() {
        if (group == "android-startup") {
            android::StartScreen screen([](QWidget *) {
                return std::unique_ptr<WindowPresentation>{};
            });
            // Android startup uses logical dimensions, independently of reader density.
            screen.resize(size / 2);
            screen.show();
            capture("no-last-book", "androidOpenBook", &screen);
            screen.setLastBook(
                {{"path", "/alice.epub"}, {"title", host.session()->title()}, {"author", host.session()->author()}});
            capture("last-book-no-cover", "androidLastBook", &screen);
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            host.session()->coverThumbnail().save(&buffer, "PNG");
            screen.setLastBook({{"path", "/alice.epub"},
                                {"title", host.session()->title()},
                                {"author", host.session()->author()},
                                {"cover", QString::fromLatin1(png.toBase64())}});
            capture("last-book", "androidLastBook", &screen);
            return;
        }

        const bool international = group.startsWith("international-");
        if (international)
            group.remove(0, 14);
        if (group == "sleep") {
            find("Alice was beginning");
            menu();
            tap("rrSettings");
            tap("rrSettingsSystem");
            capture("settings", "rrSleepScreen");
            tap("rrSleepScreen");
            capture("choices", "rrSleepScreenPopup");
            tap("rrSleep_cover");
            QImage cover;
            bool coverReady = false;
            host.session()->coverImage({2048, 2048}, [&](QImage image) {
                cover = std::move(image);
                coverReady = true;
            });
            waitFor(
                [&] {
                    return coverReady;
                },
                "Cover image did not load");
            require(!cover.isNull(), "Reference book cover is missing");
            auto *page = static_cast<ReadingPage *>(widget("rrPage"));
            const QImage reading = page->snapshot(true);
            QLabel preview(host.window());
            preview.setObjectName("rrSleepPreview");
            preview.setGeometry(host.window()->rect());
            const auto show = [&](const QString &name, qint64 remaining, int battery, reader::SleepStatus status,
                                  bool useCover = false) {
                preview.setProperty("sleep_battery", battery);
                preview.setProperty("sleep_status", reader::sleepStatusText(status));
                preview.setProperty("sleep_background", useCover ? "cover" : "page");
                preview.setPixmap(QPixmap::fromImage(reader::sleepScreenImage(
                    useCover ? reader::sleepCoverBackground(cover, reading.size()) : reading,
                    strings::get("sleep.percent_read").arg(42), reader::sleepTimeLeft(remaining), theme == "dark",
                    battery, status, !useCover)));
                preview.show();
                preview.raise();
                capture(name, "rrSleepPreview");
            };
            show("reading-page", -1, 100, reader::SleepStatus::Sleeping);
            show("with-time-left", 37 * 60000, 100, reader::SleepStatus::Sleeping);
            show("with-hours-left", 217 * 60000, 100, reader::SleepStatus::Sleeping);
            show("charging", 217 * 60000, 67, reader::SleepStatus::Charging);
            show("charged", 217 * 60000, 100, reader::SleepStatus::Charged);
            show("cover-sleeping", 217 * 60000, 100, reader::SleepStatus::Sleeping, true);
            show("cover-charging", 217 * 60000, 67, reader::SleepStatus::Charging, true);
            show("cover-charged", 217 * 60000, 100, reader::SleepStatus::Charged, true);
            return;
        }
        if (group == "native-ui") {
            find("Alice was beginning");
            capture("reading-defaults");
            menu();
            capture("menu-defaults", "rrBottomNavigation");
            tap("rrNavChapters");
            capture("chapters-defaults", "rrChoices");
            pageTap();
            menu();
            tap("rrSettings");
            capture("display-defaults", "rrDarkMode");
            tap("rrSettingsDefaults");
            capture("advanced-defaults", "rrInheritTextLayout");
            tap("rrSettingsUI");
            capture("ui-defaults", "rrHideStatusBar");
            tap("rrHideStatusBar");
            tap("rrSettings");
            menu();
            capture("menu-device-chrome", "rrMenuClock");
            tap("rrSettings");
            tap("rrSettingsDisplay");
            capture("display-device-options", "rrScreenRefresh");
            tap("rrSettingsUI");
            capture("ui-device-chrome", "rrHideStatusBar");
            return;
        }
        if (international) {
            const auto chapters = host.session()->chapters();
            require(!chapters.isEmpty(), "Public-domain book has no chapters");
            require(host.session()->goTo(chapters.first().position), "First literary chapter did not open");
            const bool vertical = group == "writing-modes" || group.endsWith("-vertical");
            settled();
            capture(vertical ? "vertical-chapter" : "reading");
            if (group == "writing-modes") {
                require(chapters.size() >= 3 && host.session()->goTo(chapters[1].position),
                        "Horizontal chapter did not open");
                settled();
                require(host.session()->goTo(chapters[2].position), "Next vertical chapter did not open");
                settled();
            }
            // These three views cover LTR, RTL and vertical chapter titles.
            if (group == "greek" || group == "arabic" || group == "writing-modes") {
                menu();
                tap("rrNavChapters");
                capture("chapters", "rrChoices");
                supplementalFonts();
            } else {
                menu();
                supplementalFonts();
            }
            return;
        }
        if (group == "stats-dense") {
            require(host.session()->chapters().size() >= 100, "Long stats fixture has fewer than one hundred chapters");
            find("But look! here come more crowds");
            menu();
            tap("rrNavStats");
            capture("dense-chapter-graph", "rrChapterGraph");
            auto *graph = dynamic_cast<ChapterGraph *>(widget("rrChapterGraph"));
            auto *bars = widget("rrChapterBars");
            require(graph && graph->rect().contains(QRect(bars->mapTo(graph, QPoint()), bars->size())),
                    "Dense chart is clipped by its panel");
            // Dense mode fits all chapters; sparse mode exposes the supported pager.
            tap("rrSettings");
            tap("rrSettingsDisplay");
            revealControlOption("rrDenseGraph");
            tap("rrDenseGraph");
            tap("rrSettings");
            pageTap();
            menu();
            tap("rrNavStats");
            require(graph->graphPages() > 1 && graph->graphPage() == 0,
                    "Sparse hundred-chapter chart did not paginate");
            const int graphTurns = checkPagerEnds("rrGraph", graph);
            require(graphTurns == graph->graphPages() - 1 && graph->graphPage() == 0,
                    "Not every chart page was reachable");
            return;
        }
        if (group == "maths") {
            // The pinned Gutenberg edition contains raster and SVG equations.
            find("IN this chapter we go to work finding solutions");
            for (int i = 0; i < 4; ++i) {
                capture(QString("equations-%1").arg(i + 1));
                host.session()->go(host.session()->page() + 1);
                settled();
            }
            return;
        }
        if (group == "footnotes") {
            find("small quantity of the second order");
            QPoint target;
            for (int n = 0; n < 4 && target.isNull(); ++n) {
                for (const auto &link : host.session()->pageLinks()) {
                    auto details = std::make_shared<LinkDetails>();
                    auto done = std::make_shared<bool>(false);
                    host.session()->linkDetails(link.source, [details, done](LinkDetails value) {
                        *details = std::move(value);
                        *done = true;
                    });
                    waitFor(
                        [&] {
                            return *done;
                        },
                        "Footnote details unavailable");
                    if (details->kind == LinkKind::Footnote) {
                        target = link.rectangles.first().center();
                        break;
                    }
                }
                if (target.isNull()) {
                    host.session()->go(host.session()->page() + 1);
                    settled();
                }
            }
            require(!target.isNull(), "No footnote near the pinned reference");
            const QString origin = host.session()->state().value("position").toString();
            const int originPage = host.session()->page();
            require(!origin.isEmpty(), "Footnote origin position is missing");
            capture("reference");
            hold(target);
            capture("actions", "rrLinkActions");
            tap("rrLinkGo");
            capture("preview", "rrLinkPanel");
            tap("rrLinkOpen");
            capture("passage");
            menu();
            capture("return-row", "rrScrubberReturn");
            tap("rrScrubberReturn");
            waitFor(
                [&] {
                    return !host.session()->busy() && host.session()->page() == originPage &&
                           host.session()->state().value("position").toString() == origin;
                },
                "Footnote return did not restore the original passage");
            return;
        }
        if (group == "dictionary") {
            dictionary("sister");
            return;
        }
        find(group == "embedded-fonts" ? "Embedded family text" : "sister");
        if (group == "sync") {
            const auto origin = host.session()->state().value("position").toString();
            find("Queen of Hearts");
            auto progress = host.progress->current;
            progress.location = host.session()->state().value("position").toString();
            host.session()->goTo(origin);
            settled();
            progress.modified = 200;
            host.progress->publish(progress);
            host.window()->progressTick();
            capture("new-position", "rrSyncedPosition");
            tap("rrKeepPosition");
            progress.modified = 300;
            progress.location = "unavailable-fixture-position";
            host.progress->publish(progress);
            host.window()->progressTick();
            capture("unavailable-position", "rrSyncedPosition");
            tap("rrGoToSyncedPosition");
            waitFor(
                [&] {
                    return widget("rrKeepPosition")->isEnabled();
                },
                "Failed sync did not finish");
            capture("position-error", "rrSyncedPosition");
            return;
        }
        if (group == "bionic") {
            find("Alice was beginning");
            capture("off");
            require(host.session()->setWeight(8), "Could not set font-preview weight");
            settled();
            menu();
            tap("rrNavText");
            tap("rrFont");
            capture("weighted-fonts", "rrChoices");
            tap("rrFontClose");
            pageTap();
            require(host.session()->setWeight(0), "Could not reset font-preview weight");
            settled();
            for (const auto *mode : {"bold"}) {
                menu();
                tap("rrSettings");
                tap("rrBionicReading");
                capture(QString(mode) + "-setting", "rrBionicReading");
                // The settings sheet covers the page center; use its toolbar action to apply.
                tap("rrSettings");
                waitFor(
                    [&] {
                        return !host.session()->busy() &&
                               host.session()->state().value("bold_reading").toString() == mode;
                    },
                    "Bionic apply did not finish", 60000);
                require(host.session()->state().value("bold_reading").toString() == mode,
                        "Bionic choice was not applied");
                if (visible("rrControlsTop"))
                    pageTap();
                find("Alice was beginning");
                capture(mode);
                menu();
                tap("rrNavText");
                capture(QString(mode) + "-weight", "rrFontWeight");
                tap("rrFont");
                capture(QString(mode) + "-fonts", "rrChoices");
                tap("rrFontClose");
                pageTap();
            }
            return;
        }
        if (group == "bluetooth") {
            menu();
            capture("off", "rrBottomNavigation");
            require(bool(bluetoothChanged), "Bluetooth subscription missing");
            bluetoothChanged(true);
            settled();
            capture("on", "rrMenuBluetooth");
            pageTap();
            require(!visible("rrMenuBluetooth"), "Bluetooth visible over the reading page");
            capture("reading");
            menu();
            bluetoothChanged(false);
            settled();
            require(!visible("rrMenuBluetooth"), "Disabled Bluetooth remains visible");
            capture("disabled", "rrBottomNavigation");
            return;
        }
        if (group == "reading" || group == "reading-long") {
            capture("reading");
            menu();
            capture("menu", "rrBottomNavigation");
            tap("rrNavSeek");
            auto *initialThumbnails = static_cast<ui::PageScrubber *>(widget("rrPageScrubber"));
            waitFor(
                [&] {
                    return initialThumbnails->previewsReady();
                },
                "Initial menu thumbnails did not render");
            capture("seek", "rrBottomNavigation");
            const int original = host.session()->page();
            auto *scrubber = dynamic_cast<ui::PageScrubber *>(widget("rrPageScrubber"));
            require(scrubber, "Missing page scrubber");
            scrubber->slider()->committed(original + 1);
            waitFor(
                [&] {
                    return scrubber->previewsReady();
                },
                "Page preview did not render");
            require(host.session()->page() == original, "Preview moved the reading position");
            capture("page-preview", "rrPageScrubber");
            if (visible("rrScrubberOverviewToggle")) {
                tap("rrScrubberOverviewToggle");
                capture("map-expanded", "rrPageScrubber");
                tap("rrScrubberZoomIn");
                require(visible("rrScrubberOverview"), "Missing zoom overview");
                capture("zoomed", "rrPageScrubber");
                scrubber->slider()->committed(original + 1);
                waitFor(
                    [&] {
                        return scrubber->previewsReady();
                    },
                    "Zoomed page preview did not render");
                capture("zoomed-preview", "rrPageScrubber");
                int zoomSteps = 0;
                while (widget("rrScrubberZoomIn")->isEnabled()) {
                    tap("rrScrubberZoomIn");
                    ++zoomSteps;
                }
                capture("maximum-zoom", "rrPageScrubber");
                while (zoomSteps-- > 0) {
                    tap("rrScrubberZoomOut");
                }
                tap("rrScrubberZoomOut");
                tap("rrScrubberOverviewToggle");
            } else {
                capture("zoom-unavailable", "rrPageScrubber");
            }
            tap("rrNavigationNext");
            waitFor(
                [&] {
                    return scrubber->previewsReady();
                },
                "Chapter preview did not render");
            capture("chapter-preview", "rrPageScrubber");
            const int destination = scrubber->selectedPage();
            clickAt(scrubber->mapTo(canvas->surface(), scrubber->previewBounds().center()), scrubber);
            settled();
            require(host.session()->page() == destination, "Thumbnail selection missed its destination");
            require(!visible("rrControlsTop") && !visible("rrBottomNavigation") && !scrubber->isVisible(),
                    "Thumbnail selection left the reading controls open");
            capture("preview-destination");
            menu();
            capture("preview-return", "rrScrubberReturn");
            tap("rrScrubberReturn");
            require(host.session()->page() == original, "Thumbnail return missed the original passage");
            capture("preview-returned");
            supplementalFonts();
            return;
        }
        if (group == "links" || group == "links-legacy" || group == "links-long") {
            host.session()->go(0);
            settled();
            QPoint target;
            for (int n = 0; n < 15 && target.isNull(); ++n) {
                for (const auto &link : host.session()->pageLinks()) {
                    auto details = std::make_shared<LinkDetails>();
                    auto done = std::make_shared<bool>(false);
                    host.session()->linkDetails(link.source, [details, done](LinkDetails value) {
                        *details = std::move(value);
                        *done = true;
                    });
                    waitFor(
                        [&] {
                            return *done;
                        },
                        "Link details unavailable");
                    if (details->kind == LinkKind::Passage) {
                        target = link.rectangles.first().center();
                        break;
                    }
                }
                if (target.isNull()) {
                    host.session()->go(host.session()->page() + 1);
                    settled();
                }
            }
            require(!target.isNull(), "No internal link found in book front matter");
            const int contentsPage = host.session()->page();
            const bool standalone = group == "links-legacy";
            const char *returnControl = "rrScrubberReturn";
            const char *linksControl = "rrScrubberLinks";
            capture("contents");
            menu();
            capture("links-controls", linksControl);
            tap(linksControl);
            capture("links-revealed", "rrLinkDiscoveryExit");
            pageTap();
            menu();
            tap("rrSettings");
            tap("rrSettingsControls");
            auto *indicatorOption = qobject_cast<QCheckBox *>(widget("rrShowLinkIndicator"));
            require(indicatorOption, "Link indicator setting missing");
            indicatorOption->setChecked(true);
            tap("rrSettings");
            settled();
            pageTap();
            capture("link-indicator", "rrLinkIndicator");
            hold(target);
            waitFor(
                [&] {
                    return visible("rrLinkActions");
                },
                "Link actions did not open");
            capture("actions", "rrLinkActions");
            tap("rrLinkGo");
            capture("destination");
            menu();
            capture("return-row", returnControl);
            // Back on the contents page by page turns, both rows share the menu.
            pageTap();
            host.session()->go(contentsPage);
            settled();
            menu();
            require(visible(linksControl), "Links action missing beside the return action");
            capture("links-with-return", returnControl);
            if (!standalone) {
                tap("rrNavigationNext");
                auto *scrubber = dynamic_cast<ui::PageScrubber *>(widget("rrPageScrubber"));
                waitFor(
                    [&] {
                        return scrubber->previewsReady();
                    },
                    "Stacked preview did not render");
                capture("links-with-return-preview", "rrScrubberActions");
                if (visible("rrScrubberOverviewToggle")) {
                    tap("rrScrubberOverviewToggle");
                    tap("rrScrubberZoomIn");
                    waitFor(
                        [&] {
                            return scrubber->previewsReady();
                        },
                        "Zoomed action preview did not render");
                    capture("links-with-return-zoomed-preview", "rrPageScrubber");
                    scrubber->cancel();
                    capture("links-with-return-zoomed", "rrPageScrubber");
                    tap("rrScrubberZoomOut");
                    scrubber->cancel();
                } else {
                    scrubber->cancel();
                    capture("links-with-return-zoom-unavailable", "rrPageScrubber");
                }
            }
            tap(returnControl);
            capture("returned");
            host.session()->go(0);
            settled();
            for (int n = 0; n < 15 && host.session()->pageLinks().isEmpty(); ++n) {
                host.session()->go(host.session()->page() + 1);
                settled();
            }
            require(!host.session()->pageLinks().isEmpty(), "Alice website link missing");
            hold(host.session()->pageLinks().first().rectangles.first().center());
            tap("rrLinkGo");
            capture("website-link", "rrBrowserConfirmation");
            return;
        }
        menu();
        if (group == "settings") {
            tap("rrNavText");
            tap("rrFont");
            tap("rrFontClose");
            tap("rrSettings");
            for (const auto &tab : {"Display", "Controls", "Defaults", "Styling", "System", "About"}) {
                tap("rrSettings" + QString(tab));
                capture(QString(tab).toLower(), "rrGeneralSettings");
                auto *next = qobject_cast<QPushButton *>(widget("rrSettingsOptionsNext"));
                int page = 1;
                while (next->isVisible() && next->isEnabled()) {
                    tap("rrSettingsOptionsNext");
                    capture(QString(tab).toLower() + QString("-page-%1").arg(++page), "rrGeneralSettings");
                    require(page < 100, "Settings pager did not reach its end");
                }
                auto *previous = qobject_cast<QPushButton *>(widget("rrSettingsOptionsPrevious"));
                while (previous->isVisible() && previous->isEnabled())
                    tap("rrSettingsOptionsPrevious");
                if (QString(tab) == "System") {
                    for (const auto &font : {"Ui", "UiMono", "HeaderFooter"}) {
                        revealControlOption("rr" + QString(font) + "Font");
                        tap("rr" + QString(font) + "Font");
                        capture(QString(font).toLower() + "-font-picker", "rrSettingsFontPopup");
                        QTest::keyClick(widget("rrSettingsFontPopup"), Qt::Key_Escape);
                        settled();
                    }
                    revealControlOption("rrHeaderFooterWeight");
                    tap("rrHeaderFooterWeight");
                    capture("headerfooter-weight-picker", "rrHeaderFooterWeightPopup");
                    QTest::keyClick(widget("rrHeaderFooterWeightPopup"), Qt::Key_Escape);
                    settled();
                    revealControlOption("rrHideTabLabels");
                    capture("hide-tab-labels", "rrHideTabLabels");
                    tap("rrHideTabLabels");
                    tap("rrSettings");
                    capture("tabs-icons-only", "rrBottomNavigation");
                    tap("rrSettings");
                    tap("rrSettingsSystem");
                    revealControlOption("rrHideTabLabels");
                    tap("rrHideTabLabels");
                    tap("rrSettings");
                    capture("tabs-with-labels", "rrBottomNavigation");
                    tap("rrSettings");
                }
                if (QString(tab) == "Defaults") {
                    revealControlOption("rrDefaultFont");
                    tap("rrDefaultFont");
                    capture("default-font", "rrSettingsFontPopup");
                    QTest::keyClick(widget("rrSettingsFontPopup"), Qt::Key_Escape);
                    settled();
                }
                if (QString(tab) == "Styling") {
                    for (const auto &family : {"Serif", "Sans", "Mono"}) {
                        tap("rr" + QString(family) + "Font");
                        capture("font-fallback-" + QString(family).toLower(), "rrSettingsFontPopup");
                        if (QString(family) == "Serif") {
                            auto *next = qobject_cast<QPushButton *>(widget("rrSettingsFontNext"));
                            int fontPage = 1;
                            while (next->isVisible() && next->isEnabled()) {
                                tap("rrSettingsFontNext");
                                capture(QString("font-fallback-serif-page-%1").arg(++fontPage), "rrSettingsFontPopup");
                                require(fontPage < 100, "Fallback font pager did not reach its end");
                            }
                        }
                        auto *popup = widget("rrSettingsFontPopup");
                        QTest::keyClick(popup, Qt::Key_Escape);
                        settled();
                    }
                }
                if (QString(tab) == "Styling") {
                    revealControlOption("rrAvoidWastefulStyling");
                    capture("styling-content", "rrAvoidWastefulStyling");
                }
                if (QString(tab) == "Controls") {
                    revealControlOption("rrEdgeLight");
                    capture("controls-frontlight", "rrEdgeLight");
                    tap("rrSettingsDisplay");
                    tap("rrSettingsControls");
                }
            }
            tap("rrSettingsDictionaries");
            auto *dictionaries = dynamic_cast<reader::DictionariesPage *>(widget("rrDictionaries"));
            require(dictionaries, "Dictionary management page missing");
            const auto catalog = DictionaryManager::catalog();
            dictionaryManagementFixture = true;
            dictionaries->setLanguage("en");
            dictionaries->setCatalog(catalog);
            dictionaries->setState({"en-en", "ar-ar"}, true, {});
            dictionaries->setStorage({{"en-en", 92000000}, {"ar-ar", 3500000}}, 1000000000);
            dictionaries->setTransfer("en-fr", 3100000, 6900000, false);
            dictionaries->installRequested = [](const QString &) {};
            dictionaries->removeRequested = [](const QString &) {};
            dictionaries->cancelRequested = [] {};
            capture("dictionaries-book", "rrDictionaries");
            tap("rrDictionaryAction0");
            capture("dictionary-remove-confirmation", "rrDictionaryRemove");
            tap("rrDictionaryRemoveCancel");
            for (int i = 0; i < 3 && widget("rrDictionariesNext")->isEnabled(); ++i)
                tap("rrDictionariesNext");
            capture("dictionaries-other", "rrDictionaries");
            dictionaryManagementFixture = false;
            tap("rrSettingsControls");
            for (const auto &name :
                 {"rrPageNavigation", "rrTapZones", "rrNavigationBarButtons", "rrShowLinkIndicator"}) {
                revealControlOption(name);
                auto *row = widget(name);
                QTest::mousePress(row, Qt::LeftButton);
                capture(QString(name).mid(2).toLower() + "-pressed", name);
                QTest::mouseMove(row, QPoint(-10, -10));
                QTest::mouseRelease(row, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
            }
            revealControlOption("rrDefaultMenuView");
            tap("rrDefaultMenuView");
            capture("default-menu-view", "rrMenuViewPopup");
            tap("rrMenuView_none");
            revealControlOption("rrNavigationBarButtons");
            tap("rrNavigationBarButtons");
            capture("navigation-bar-buttons", "rrNavigationBarPopup");
            tap("rrNavigationBar_chapters");
            capture("navigation-bar-chapters-selected", "rrNavigationBarButtons");
            tap("rrNavigationBarButtons");
            tap("rrNavigationBar_pages");
            revealControlOption("rrPageNavigation");
            tap("rrPageNavigation");
            capture("page-navigation", "rrPageNavigationPopup");
            tap("rrNavigation_swipes_only");
            capture("page-navigation-selected", "rrPageNavigation");
            tap("rrPageNavigation");
            tap("rrNavigation_taps_and_swipes");
            pageButtonsSupported = false;
            tap("rrPageNavigation");
            capture("page-navigation-no-buttons", "rrPageNavigationPopup");
            tap("rrNavigation_taps_and_swipes");
            pageButtonsSupported = true;
            tap("rrTapZones");
            capture("tap-zones", "rrTapZonesPopup");
            tap("rrTapZones_reversed");
            capture("tap-zones-reversed-selected", "rrTapZones");
            tap("rrTapZones");
            capture("tap-zones-reversed", "rrTapZonesPopup");
            tap("rrTapZones_top_bottom");
            tap("rrTapZones");
            capture("tap-zones-top-bottom", "rrTapZonesPopup");
            tap("rrTapZones_one_handed");
            capture("tap-zones-one-handed-selected", "rrTapZones");
            tap("rrTapZones");
            capture("tap-zones-one-handed", "rrTapZonesPopup");
            tap("rrTapZones_one_handed");
            tap("rrPageNavigation");
            capture("one-handed-navigation", "rrPageNavigationPopup");
            tap("rrNavigation_taps_and_swipes");
            tap("rrTapZones");
            tap("rrTapZones_left_right");
            tap("rrSettingsDefaults");
            tap("rrRestoreDefaults");
            capture("restore-confirmation", "rrConfirmDefaults");
            tap("rrKeepDefaults");
            tap("rrSettingsDisplay");
            tap("rrScreenRefresh");
            capture("screen-refresh", "rrScreenRefreshPopup");
            return;
        }
        if (group == "embedded-fonts") {
            tap("rrNavText");
            tap("rrFont");
            capture("detected", "rrChoices");
            tap("rrFontChoice0");
            capture("publisher-selected", "rrChoices");
            return;
        }
        if (group == "typography") {
            tap("rrNavText");
            capture("text", "rrFont");
            auto *idleProgress = widget("rrApplyingProgress");
            idleProgress->setObjectName("rrIdleApplyingProgress");
            {
                // Reader refreshes dismiss its inactive render notice. Use the
                // same control independently so it stays visible during capture.
                ui::ModalOverlay loading(host.window());
                loading.setDelayedProgress(true, 999, 37);
                capture("applying-before-delay", "rrFont");
                loading.setDelayedProgress(true, 1000, 37, strings::get("reader.applying_appearance_changes"));
                capture("applying-progress", "rrApplyingProgress");
                loading.setDelayedProgress(true, 1000, 37, strings::get("reader.enabling_bold_reading"));
                capture("enabling-bionic", "rrApplyingProgress");
                loading.setDelayedProgress(true, 1000, 37, strings::get("reader.disabling_bold_reading"));
                capture("disabling-bionic", "rrApplyingProgress");
            }
            idleProgress->setObjectName("rrApplyingProgress");
            tap("rrFont");
            const QStringList sections{"RECOMMENDED FONTS", "BUILT-IN FONTS", "SIDELOADED FONTS"};
            for (int i = 0; i < sections.size(); ++i) {
                auto *section = qobject_cast<QLabel *>(widget("rrFontSection"));
                require(section && section->text() == sections[i], "Missing font section: " + sections[i]);
                capture(QString("fonts-%1").arg(i + 1), "rrChoices");
                if (i + 1 < sections.size())
                    tap("rrFontPagerNext");
            }
            while (!qobject_cast<QLabel *>(widget("rrFontPagerPage"))->text().startsWith("1 of "))
                tap("rrFontPagerPrevious");
            // Synthetic names exercise wrapping without installing extra fonts.
            const QStringList longNames{"Atkinson Hyperlegible Next", "Source Serif Pro Semibold",
                                        "OneExtremelyLongUnbrokenFontName"};
            QVector<QPair<ui::SampleLabel *, QString>> originalNames;
            for (int i = 0; i < longNames.size(); ++i) {
                auto *label = dynamic_cast<ui::SampleLabel *>(widget(QString("rrFontChoice%1Title").arg(i + 2)));
                require(label, "Missing long-name font sample");
                originalNames.append({label, label->text()});
                label->setText(longNames[i]);
            }
            capture("fonts-long-names", "rrChoices");
            for (const auto &entry : originalNames)
                entry.first->setText(entry.second);
            tap("rrFontDefaultSlot");
            capture("default-instructions", "rrFontDefaultDialog");
            tap("rrFontDefaultConfirm");
            QWidget *candidate = nullptr;
            for (auto *label : host.window()->findChildren<QLabel *>())
                if (label->objectName().endsWith("Title") && label->text() == "Ancizar Serif")
                    candidate = label->parentWidget();
            require(candidate, "Missing default font candidate");
            QContextMenuEvent event(QContextMenuEvent::Keyboard, candidate->rect().center());
            QApplication::sendEvent(candidate, &event);
            capture("default-confirmation", "rrFontDefaultDialog");
            tap("rrFontDefaultConfirm");
            capture("custom-default", "rrChoices");
            tap("rrFontDefaultSlot");
            capture("custom-default-instructions", "rrFontDefaultDialog");
            tap("rrFontDefaultConfirm");
            tap("rrFontClose");
            tap("rrLarger");
            capture("size-preview", "rrFont");
            tap("rrHeavier");
            capture("weight-preview", "rrFont");
            tap("rrLooser");
            capture("spacing-preview", "rrFont");
            return;
        }
        if (group == "layout-saved-field") {
            tap("rrNavLayout");
            tap("rrHeaderFooter");
            require(qobject_cast<QAbstractButton *>(widget("rrHeaderFooterPreset_custom"))->isChecked(),
                    "New book did not inherit the saved Custom layout");
            tap("rrHeaderFooterSlot3");
            capture("first-open", "rrHeaderFooterChooser");
            return;
        }
        if (group == "layout" || group == "layout-system-rotation") {
            tap("rrNavLayout");
            capture("layout", "rrMargins");
            tap("rrWider");
            tap("rrVerticalMarginsMore");
            capture("margin-preview", "rrMargins");
            if (group == "layout-system-rotation")
                return;
            // Header and footer: its view and one slot's choices.
            tap("rrHeaderFooter");
            capture("header-footer", "rrHeaderFooterView");
            tap("rrHeaderFooterSlot2");
            capture("header-footer-fields", "rrHeaderFooterChooser");
            tap("rrHeaderFooterFieldsClose");
            // Large strips make the preview page, and the sheet, taller.
            tap("rrHeaderTextSize_large");
            tap("rrFooterTextSize_large");
            capture("header-footer-large", "rrHeaderFooterView");
            tap("rrHeaderTextSize_small");
            tap("rrFooterTextSize_small");
            // The Layout tab closes the view and the panel. Then show each
            // preset on the page, with the menus closed.
            tap("rrNavLayout");
            for (const auto *preset : {"default", "alternative_a", "alternative_b"}) {
                menu();
                tap("rrNavLayout");
                tap("rrHeaderFooter");
                tap(QString("rrHeaderFooterPreset_") + preset);
                tap("rrNavLayout");
                // A new strip height reflows the book before the menu settles.
                waitFor(
                    [&] {
                        return !host.session()->busy() && !visible("rrMenu");
                    },
                    "Layout panel did not close");
                for (int attempt = 0; attempt < 3 && visible("rrControlsTop"); ++attempt)
                    pageTap();
                require(!visible("rrControlsTop"), "Reading menu did not close");
                capture(QString("reading-") + QString(preset).replace('_', '-'));
            }
            return;
        }
        if (group == "chapters") {
            tap("rrNavChapters");
            for (int i = 0; i < 100; ++i) {
                capture(QString("chapters-%1").arg(i + 1, 2, 10, QChar('0')), "rrContents");
                if (lastListPage("rrContents"))
                    return;
                tap("rrContentsNext");
            }
            throw std::runtime_error("Chapter picker did not reach its last page");
        }
        if (group == "light-sensor") {
            tap("rrNavLight");
            tap("rrAutomaticBrightness");
            tap("rrAutomaticWarmth");
            auto *brightness = qobject_cast<QCheckBox *>(widget("rrAutomaticBrightness"));
            auto *warmth = qobject_cast<QCheckBox *>(widget("rrAutomaticWarmth"));
            require(brightness && brightness->isChecked(), "Automatic brightness did not turn on");
            require(warmth && warmth->isChecked(), "Automatic warmth did not turn on");
            capture("frontlight-both-automatic", "rrLightPanel");
            return;
        }
        if (group == "light") {
            tap("rrNavLight");
            capture("frontlight", "rrLightPanel");
            auto *automaticWarmth = qobject_cast<QCheckBox *>(widget("rrAutomaticWarmth"));
            require(automaticWarmth, "Automatic warmth checkbox missing");
            tap("rrAutomaticWarmth");
            require(automaticWarmth->isChecked(), "Automatic warmth did not turn on");
            capture("frontlight-automatic-warmth", "rrLightPanel");
            auto *slider = qobject_cast<QSlider *>(widget("rrBrightness"));
            require(slider, "Brightness slider missing");
            const int initial = slider->value();
            tap("rrNavLight");
            pageTap();
            require(!visible("rrControlsTop") && !visible("rrLightPanel"), "Light controls did not close before swipe");
            const auto position = host.session()->state().value("position").toString();
            require(!position.isEmpty(), "Reader position missing before brightness swipe");
            auto *page = widget("rrPage");
            const QPoint start(page->width() / 20, page->height() * 3 / 4);
            const QPoint end(start.x(), page->height() / 4);
            QTest::mousePress(page, Qt::LeftButton, Qt::NoModifier, start);
            QMouseEvent move(QEvent::MouseMove, end, page->mapToGlobal(end), Qt::NoButton, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(page, &move);
            require(visible("rrBrightnessNotice"), "Brightness swipe indicator did not appear");
            auto *valueLabel = qobject_cast<QLabel *>(widget("rrLightSwipeValue"));
            require(valueLabel, "Brightness percentage missing");
            bool valid = false;
            const int value = valueLabel->text().chopped(1).toInt(&valid);
            require(valid && value > initial && value <= 100,
                    "Brightness swipe did not change the displayed percentage");
            // Keep the pointer down so the hide timer cannot expire during capture.
            capture("brightness-swipe", "rrBrightnessNotice");
            QTest::mouseRelease(page, Qt::LeftButton, Qt::NoModifier, end);
            settled();
            require(host.session()->state().value("position").toString() == position,
                    "Brightness swipe changed reader position");
            menu();
            tap("rrNavLight");
            require(slider->value() > initial, "Brightness swipe did not update the light service");
            require(automaticWarmth->isChecked(), "Brightness swipe changed automatic warmth");
            tap("rrNavLight");
            pageTap();
            const QPoint warmStart(page->width() * 19 / 20, page->height() / 4);
            const QPoint coolEnd(warmStart.x(), page->height() * 3 / 4);
            QTest::mousePress(page, Qt::LeftButton, Qt::NoModifier, warmStart);
            QMouseEvent coolMove(QEvent::MouseMove, coolEnd, page->mapToGlobal(coolEnd), Qt::NoButton, Qt::LeftButton,
                                 Qt::NoModifier);
            QApplication::sendEvent(page, &coolMove);
            require(visible("rrBrightnessNotice") && valueLabel->text() == "0%",
                    "Warmth swipe did not reach its cool endpoint");
            capture("warmth-swipe-cool", "rrBrightnessNotice");
            QTest::mouseRelease(page, Qt::LeftButton, Qt::NoModifier, coolEnd);
            settled();
            const QPoint warmEnd(coolEnd.x(), coolEnd.y() - page->height() * 8 / 15);
            QTest::mousePress(page, Qt::LeftButton, Qt::NoModifier, coolEnd);
            QMouseEvent warmMove(QEvent::MouseMove, warmEnd, page->mapToGlobal(warmEnd), Qt::NoButton, Qt::LeftButton,
                                 Qt::NoModifier);
            QApplication::sendEvent(page, &warmMove);
            require(visible("rrBrightnessNotice") && valueLabel->text() == "80%",
                    "Warmth swipe did not reach the thick-ray threshold");
            capture("warmth-swipe", "rrBrightnessNotice");
            QTest::mouseRelease(page, Qt::LeftButton, Qt::NoModifier, warmEnd);
            settled();
            require(host.session()->state().value("position").toString() == position,
                    "Warmth swipe changed reader position");
            menu();
            tap("rrNavLight");
            require(!automaticWarmth->isChecked(), "Warmth swipe did not turn off automatic warmth");
            return;
        }
        if (group == "stats") {
            tap("rrNavStats");
            capture("chapter-graph", "rrChapterGraph");
            return;
        }
        if (group == "search") {
            tap("rrSearchOpen");
            capture("empty", "rrSearch");
            auto *query = qobject_cast<QLineEdit *>(widget("rrSearchQuery"));
            require(query, "Search editor missing");
            query->setText("Alice");
            capture("query", "rrSearch");
            tap("rrSearchSubmit");
            waitFor(
                [&] {
                    return visible("rrSearchResult0") && !widget("rrSearchStatus")
                                                              ->property("text")
                                                              .toString()
                                                              .contains("searching", Qt::CaseInsensitive);
                },
                "Search has not finished");
            capture("results", "rrSearch");
            if (widget("rrSearchNext")->isEnabled()) {
                tap("rrSearchNext");
                capture("results-page-2", "rrSearch");
                tap("rrSearchPrevious");
            }
            tap("rrSearchResult0");
            menu();
            tap("rrSearchOpen");
            capture("return-and-edit", "rrSearch");
            require(visible("rrSearchOriginal"), "Search original-position action missing");
            query->setText("zzzzmissingword");
            tap("rrSearchSubmit");
            waitFor(
                [&] {
                    return !visible("rrSearchResult0") &&
                           widget("rrSearchStatus")->property("text").toString().contains("No matches");
                },
                "No-result search did not finish");
            capture("no-results", "rrSearch");
            // Reader's first refresh replays icons before its search panel opens.
            // Construct this separate panel to retain the pixels before that replay.
            QWidget surface;
            surface.resize(size);
            surface.setProperty("reader_dark", theme == "dark");
            surface.setPalette(host.window()->palette());
            surface.setFont(ui::interfaceFont());
            ui::readerStyle(&surface, ui::styles::reader());
            SearchPanel probe(&surface, test::hostReaderServices().keyboard);
            probe.setGeometry(surface.rect());
            surface.show();
            probe.open(false, 0);
            auto *probeQuery = probe.findChild<QLineEdit *>("rrSearchQuery");
            auto *clear = probe.findChild<QPushButton *>("rrSearchClear");
            require(probeQuery && clear, "Search clear probe controls missing");
            probeQuery->setText("Alice");
            // Search uses physical reader metrics, unlike the logical Android surface.
            capture("clear-construction", "rrSearchClear", &surface, 1);
            ui::refreshButtonIcon(clear);
            capture("clear-replayed", "rrSearchClear", &surface, 1);
            const auto replayed = clear->grab().toImage();
            ui::refreshButtonIcon(clear);
            require(clear->grab().toImage() == replayed, "Repeated search clear replay changes pixels");
            return;
        }
        if (group == "annotations" || group == "menu-thumbnails") {
            auto savedHighlights = [&] {
                QDirIterator files(paths.annotations(), {"highlights.json"}, QDir::Files, QDirIterator::Subdirectories);
                QJsonArray entries;
                int stores = 0;
                while (files.hasNext()) {
                    const auto saved = storage::readJsonFile(files.next(), HighlightStore::maxFileBytes);
                    require(saved.status == storage::ReadStatus::Ok, "Saved highlights are unreadable");
                    entries = saved.object.value("highlights").toArray();
                    ++stores;
                }
                require(stores <= 1, "Annotation tour created multiple highlight stores");
                return entries;
            };
            auto waitForSavedPaint = [&] {
                // Saving first resolves stored pages, then queues their visible geometry.
                // Two following replies let both earlier jobs finish before image checks.
                const auto saved = savedHighlights();
                const auto entry = saved.isEmpty() ? QJsonObject{{"start", host.session()->state().value("position")},
                                                                 {"end", host.session()->state().value("position")}}
                                                   : saved.first().toObject();
                for (int phase = 0; phase < 2; ++phase) {
                    auto done = std::make_shared<bool>(false);
                    host.session()->rangeRects(entry.value("start").toString(), entry.value("end").toString(),
                                               [done](QVector<QRect>) {
                                                   *done = true;
                                               });
                    waitFor(
                        [&] {
                            return *done;
                        },
                        "Saved annotation geometry barrier timed out");
                }
            };
            auto chooseBoundary = [&](const QString &action) {
                const auto before = savedHighlights();
                tap(action);
                waitFor(
                    [&] {
                        return savedHighlights() != before;
                    },
                    "Boundary choice did not persist its changed range");
                require(!visible("rrAnnotationPopover"), "Successful save opened another popup");
                require(visible("rrAnnotationHeader"), "Successful save left annotation mode");
                require(!host.window()->findChild<QWidget *>("rrAnnotationUndo"), "Removed saved Undo control remains");
                waitForSavedPaint();
            };
            auto editSavedAt = [&](QPoint point) {
                auto *page = host.window()->findChild<ReadingPage *>("rrPage");
                clickAt(page->mapTo(canvas->surface(), point + QPoint(0, page->topHeight())), page);
                waitFor(
                    [&] {
                        return visible("rrAnnotationEdit") && visible("rrAnnotationRemove");
                    },
                    "Saved range did not reopen its deliberate actions");
                tap("rrAnnotationEdit");
            };
            tap("rrBookmarks");
            tap("rrAnnotationsTab");
            capture("empty", "rrBookmarkList");
            tap("rrBookmarkListClose");
            const auto first = find("Alice was beginning");
            const auto firstPoint = selection(first);
            menu();
            const QImage image = host.window()->findChild<ReadingPage *>("rrPage")->image;
            const auto position = host.session()->state().value("position");
            tap("rrAnnotate");
            require(!host.window()->findChild<QWidget *>("rrAnnotationFooter"), "Annotation mode kept its bottom bar");
            require(host.window()->findChild<ReadingPage *>("rrPage")->image == image &&
                        host.session()->state().value("position") == position,
                    "Annotation mode changed the reading image or position");
            capture("mode", "rrAnnotationHeader");
            auto startAt = [&](QPoint point) {
                auto *page = host.window()->findChild<ReadingPage *>("rrPage");
                clickAt(page->mapTo(canvas->surface(), point + QPoint(0, page->topHeight())), page);
                waitFor(
                    [&] {
                        return page->annotationContains(point);
                    },
                    "First tap did not mark its start");
                require(!visible("rrAnnotationPopover"), "First tap opened a word menu");
                require(savedHighlights().isEmpty(), "First tap persisted a highlight");
            };
            auto tapWord = [&](QPoint point) {
                auto *page = host.window()->findChild<ReadingPage *>("rrPage");
                clickAt(page->mapTo(canvas->surface(), point + QPoint(0, page->topHeight())), page);
                waitFor(
                    [&] {
                        return visible("rrAnnotationHighlightWord") || visible("rrAnnotationEnd");
                    },
                    "Word-specific annotation menu did not open");
                require(visible("rrAnnotationStop") && visible("rrAnnotationCancel") && visible("rrAnnotationDismiss"),
                        "Draft word menu lost Cancel, Stop or Dismiss");
                require(!visible("rrAnnotationStart") && !visible("rrAnnotationSavedOptions"),
                        "Draft word menu retained removed actions");
                require(visible("rrAnnotationHighlightWord") != visible("rrAnnotationEnd"),
                        "Same-start and other-word actions appeared together");
                waitForSavedPaint();
            };
            startAt(firstPoint);
            capture("start-outline", "rrAnnotationHeader");
            tapWord(firstPoint);
            auto *annotationPopup = widget("rrAnnotationPopover");
            auto *pointer = annotationPopup->findChild<QWidget *>("rrAnchorPointer");
            require(pointer && pointer->isVisible() && pointer->property("card_below").toBool(),
                    "First-word menu did not use the native pointer below its word");
            capture("start-choice", "rrAnnotationPopover");
            if (visible("rrAnnotationPalette")) {
                tap("rrAnnotationColor_green");
                capture("color-green-choice", "rrAnnotationPopover");
            } else {
                capture("monochrome-boundary", "rrAnnotationPopover");
            }
            require(!visible("rrAnnotationEnd"), "Retapping the chosen start offered End");
            tap("rrAnnotationDismiss");
            require(!visible("rrAnnotationPopover"), "Dismiss kept the word menu open");
            capture("candidate-dismissed", "rrAnnotationHeader");
            tapWord(firstPoint);
            capture("cancel-original-word", "rrAnnotationPopover");
            tap("rrAnnotationCancel");
            require(!visible("rrAnnotationPopover") && visible("rrAnnotationHeader"),
                    "Cancel did not clear the draft while keeping mode");
            require(savedHighlights().isEmpty(), "Cancel saved an unfinished range");
            capture("selection-cancelled", "rrAnnotationHeader");
            startAt(firstPoint);
            auto *cancelPage = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(cancelPage->mapTo(canvas->surface(), QPoint(0, cancelPage->height() / 2)), cancelPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationCancel");
                },
                "Blank-space draft menu did not expose Cancel");
            capture("cancel-blank-space", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            tapWord(firstPoint);
            tap("rrAnnotationStop");
            require(!visible("rrAnnotationSave"), "Incomplete draft offered a save");
            capture("unsaved-choice", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            tap("rrAnnotationClose");
            require(!visible("rrAnnotationHeader") && !visible("rrAnnotationPopover"),
                    "Close did not immediately discard the unfinished selection");
            require(savedHighlights().isEmpty(), "Close saved an unfinished range");
            capture("closed-unfinished-selection", "rrPage");
            menu();
            tap("rrAnnotate");
            startAt(firstPoint);
            tapWord(firstPoint);
            require(visible("rrAnnotationHighlightWord"), "Fresh word has no immediate highlight action");
            capture("highlight-word-choice", "rrAnnotationPopover");
            chooseBoundary("rrAnnotationHighlightWord");
            capture("highlight-word-saved", "rrAnnotationHeader");
            capture("one-word-saved", "rrAnnotationHeader");
            require(savedHighlights().size() == 1, "Highlight word did not autosave exactly one entry");
            if (group == "menu-thumbnails") {
                tap("rrAnnotationClose");
                auto *page = host.window()->findChild<ReadingPage *>("rrPage");
                clickAt(page->mapTo(canvas->surface(), page->bookmarkCorner().center()), page);
                waitFor(
                    [&] {
                        return page->bookmarked;
                    },
                    "Thumbnail bookmark did not save");
                menu();
                tap("rrNavSeek");
                auto *thumbnails = static_cast<ui::PageScrubber *>(widget("rrPageScrubber"));
                waitFor(
                    [&] {
                        return thumbnails->previewsReady();
                    },
                    "Highlighted menu thumbnails did not render");
                capture("menu-thumbnails-highlights", "rrPageScrubber");
                auto *next = widget("rrPreviewNextPage");
                const QRect visibleCard = next->geometry().intersected(thumbnails->rect());
                require(!visibleCard.isEmpty(), "Next thumbnail has no visible touch area");
                clickAt(thumbnails->mapTo(canvas->surface(), visibleCard.center()), next);
                waitFor(
                    [&] {
                        return thumbnails->previewsReady();
                    },
                    "Adjacent thumbnail did not render");
                capture("menu-thumbnails-bookmark-previous", "rrPageScrubber");
                return;
            }
            auto *readyPage = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(readyPage->mapTo(canvas->surface(), firstPoint + QPoint(0, readyPage->topHeight())), readyPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationEdit") && visible("rrAnnotationRemove");
                },
                "Saved highlight could not reopen its actions");
            capture("ready-retap", "rrAnnotationPopover");
            if (visible("rrAnnotationPalette")) {
                const auto beforeColor = savedHighlights().first().toObject();
                tap("rrAnnotationColor_blue");
                waitFor(
                    [&] {
                        return savedHighlights().first().toObject().value("color") == "blue";
                    },
                    "Saved palette did not persist blue");
                waitForSavedPaint();
                require(savedHighlights().first().toObject().value("id") == beforeColor.value("id") &&
                            savedHighlights().first().toObject().value("text") == beforeColor.value("text"),
                        "Recolor changed highlight identity or text");
                capture("saved-color-blue", "rrAnnotationPopover");
            } else {
                capture("monochrome-saved-actions", "rrAnnotationPopover");
            }
            tap("rrAnnotationEdit");
            auto *readingPage = host.window()->findChild<ReadingPage *>("rrPage");
            QPoint bottomWord;
            const QRect banner = widget("rrAnnotationHeader")->geometry();
            for (int y = readingPage->image.height() - ui::px(70);
                 bottomWord.isNull() && y > readingPage->image.height() * 4 / 5; y -= ui::px(20)) {
                for (int fraction : {2, 1, 3}) {
                    auto word = std::make_shared<WordSelection>();
                    auto done = std::make_shared<bool>(false);
                    host.session()->wordAt({readingPage->image.width() * fraction / 4, y},
                                           [word, done](WordSelection value) {
                                               *word = std::move(value);
                                               *done = true;
                                           });
                    waitFor(
                        [&] {
                            return *done;
                        },
                        "Bottom-word lookup timed out");
                    if (!word->rectangles.isEmpty()) {
                        const QRect wordBounds(
                            readingPage->mapTo(host.window(), word->rectangles.first().topLeft() +
                                                                  QPoint(0, readingPage->topHeight())),
                            word->rectangles.first().size());
                        if (banner.intersects(wordBounds)) {
                            continue;
                        }
                        bottomWord = word->rectangles.first().center();
                        break;
                    }
                }
            }
            require(!bottomWord.isNull(), "No bottom-of-page annotation word available");
            tapWord(bottomWord);
            require(!pointer->property("card_below").toBool(), "Bottom-word menu did not point down from above");
            capture("bottom-boundary-choice", "rrAnnotationPopover");
            chooseBoundary("rrAnnotationEnd");
            const auto expandedStored = savedHighlights();
            const QImage expandedDraft = readingPage->snapshot();
            editSavedAt(bottomWord);
            tapWord(bottomWord);
            capture("edit-end-only", "rrAnnotationPopover");
            tap("rrAnnotationCancel");
            waitForSavedPaint();
            require(savedHighlights() == expandedStored && readingPage->snapshot() == expandedDraft,
                    "Cancelling an end adjustment changed the original range");
            const QPoint nearerEnd = selection(find("getting"));
            editSavedAt(firstPoint);
            tapWord(nearerEnd);
            chooseBoundary("rrAnnotationEnd");
            require(readingPage->snapshot() != expandedDraft, "Moving the end inward left the draft unchanged");
            editSavedAt(firstPoint);
            tapWord(bottomWord);
            chooseBoundary("rrAnnotationEnd");
            require(savedHighlights() == expandedStored, "Moving the end outward failed to restore the stored range");
            require(readingPage->snapshot() == expandedDraft, "Moving the end outward failed to restore the range");
            editSavedAt(bottomWord);
            require(!visible("rrAnnotationPopover"), "Adjust kept the saved popup open");
            const auto last = find("Dinah");
            const auto lastPoint = selection(last);
            tapWord(lastPoint);
            capture("extend-choice", "rrAnnotationPopover");
            chooseBoundary("rrAnnotationEnd");
            capture("cross-page-saved", "rrAnnotationHeader");
            editSavedAt(lastPoint);
            waitForSavedPaint();
            capture("cross-page-outline", "rrAnnotationHeader");
            tap("rrAnnotationClose");
            require(!visible("rrAnnotationHeader"), "Closing unchanged edit requested a confirmation");
            capture("saved-passage");
            menu();
            auto *thumbnails = static_cast<ui::PageScrubber *>(widget("rrPageScrubber"));
            waitFor(
                [&] {
                    return thumbnails->previewsReady();
                },
                "Highlighted page thumbnails did not render");
            capture("menu-thumbnails-highlights", "rrPageScrubber");
            pageTap();
            require(!visible("rrControlsTop"), "Tapping the current thumbnail did not close the menu");
            const auto persisted = savedHighlights();
            require(persisted.size() == 1, "Cross-page highlight did not persist as one entry");
            const auto interior = find("pictures");
            const auto interiorPoint = selection(interior);
            menu();
            tap("rrAnnotate");
            auto tapInterior = [&] {
                auto *page = host.window()->findChild<ReadingPage *>("rrPage");
                clickAt(page->mapTo(canvas->surface(), interiorPoint + QPoint(0, page->topHeight())), page);
            };
            auto tapSavedWord = [&] {
                tapInterior();
                waitFor(
                    [&] {
                        return visible("rrAnnotationEdit") && visible("rrAnnotationRemove");
                    },
                    "Saved highlight word did not open its actions");
            };
            const auto overlappingStart = find("Alice was");
            auto *overlapPage = host.window()->findChild<ReadingPage *>("rrPage");
            const QPoint overlapPoint = selection(overlappingStart);
            clickAt(overlapPage->mapTo(canvas->surface(), overlapPoint + QPoint(0, overlapPage->topHeight())),
                    overlapPage);
            waitFor(
                [&] {
                    return overlapPage->annotationContains(overlapPoint);
                },
                "Overlapping draft start was not marked");
            require(!visible("rrAnnotationPopover"), "Overlapping draft first tap opened a menu");
            find("pictures");
            const QImage growingDraft = host.window()->findChild<ReadingPage *>("rrPage")->snapshot();
            tapWord(interiorPoint);
            require(!visible("rrAnnotationSavedOptions") && !visible("rrAnnotationStart"),
                    "Draft overlap retained removed actions");
            require(visible("rrAnnotationEnd") && !visible("rrAnnotationHighlightWord"),
                    "Other saved word did not offer End alone");
            capture("draft-over-saved-word", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            require(host.window()->findChild<ReadingPage *>("rrPage")->snapshot() == growingDraft,
                    "Dismissing saved options changed the growing draft");
            tapWord(interiorPoint);
            chooseBoundary("rrAnnotationEnd");
            waitFor(
                [&] {
                    return savedHighlights().size() == 2;
                },
                "Second overlapping passage did not persist");
            tapInterior();
            waitFor(
                [&] {
                    return visible("rrAnnotationMatch0") && visible("rrAnnotationMatch1");
                },
                "Overlapping saved passages did not open a chooser");
            capture("overlap-choice", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            require(savedHighlights().size() == 2, "Dismissing overlap choices changed saved passages");
            tapInterior();
            waitFor(
                [&] {
                    return visible("rrAnnotationMatch1");
                },
                "Overlap chooser did not reopen");
            tap("rrAnnotationMatch1");
            capture("overlap-target-actions", "rrAnnotationRemove");
            tap("rrAnnotationRemove");
            capture("overlap-remove-confirmation", "rrConfirmRemoveAnnotation");
            tap("rrConfirmRemoveAnnotation");
            waitFor(
                [&] {
                    return savedHighlights() == persisted;
                },
                "Removing one overlap changed the other passage");
            tapSavedWord();
            capture("saved-word-actions", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            require(savedHighlights() == persisted, "Dismissing saved actions changed persistence");
            capture("saved-word-dismissed", "rrAnnotationHeader");
            tapSavedWord();
            tap("rrAnnotationEdit");
            waitFor(
                [&] {
                    return visible("rrAnnotationHeader") && !visible("rrAnnotationPopover");
                },
                "Page edit did not arm boundary adjustment");
            capture("edit-from-page", "rrAnnotationHeader");
            tap("rrAnnotationClose");
            require(savedHighlights() == persisted, "Canceling page edit changed the saved highlight");
            reloadCreatedFixtures();
            menu();
            tap("rrBookmarks");
            tap("rrAnnotationsTab");
            waitFor(
                [&] {
                    return visible("rrAnnotation0");
                },
                "Saved passage was not listed");
            require(!visible("rrAnnotation1"), "One cross-page highlight created multiple list entries");
            capture("list", "rrBookmarkList");
            tap("rrAnnotation0");
            waitFor(
                [&] {
                    return !host.session()->busy();
                },
                "Annotation row did not navigate");
            require(!visible("rrAnnotationHeader"), "Annotation row unexpectedly entered adjustment mode");
            menu();
            tap("rrAnnotate");
            auto *listedPage = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(listedPage->mapTo(canvas->surface(), firstPoint + QPoint(0, listedPage->topHeight())), listedPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationEdit");
                },
                "Listed range did not retain in-page adjustment");
            capture("passage-actions", "rrAnnotationEdit");
            tap("rrAnnotationEdit");
            waitFor(
                [&] {
                    return visible("rrAnnotationHeader") && !visible("rrAnnotationPopover");
                },
                "List edit did not arm boundary adjustment");
            capture("edit", "rrAnnotationHeader");
            tap("rrAnnotationClose");
            menu();
            tap("rrBookmarks");
            tap("rrAnnotationsTab");
            tap("rrAnnotationRemove0");
            capture("remove-confirmation", "rrConfirmRemoveAnnotation");
            tap("rrKeepAnnotation");
            require(visible("rrAnnotation0"), "Canceling removal removed the passage");
            tap("rrBookmarkListClose");
            if (visible("rrControlsTop")) {
                pageTap();
            }
            find("pictures");
            menu();
            tap("rrAnnotate");
            tapSavedWord();
            tap("rrAnnotationRemove");
            capture("page-remove-confirmation", "rrConfirmRemoveAnnotation");
            tap("rrConfirmRemoveAnnotation");
            waitFor(
                [&] {
                    return savedHighlights().isEmpty();
                },
                "Page removal kept the cross-page highlight");
            capture("removed-passage", "rrAnnotationHeader");
            const auto punctuationHit = find(QString::fromUtf8("conversations?”"));
            const auto punctuationPoint = selection(punctuationHit);
            startAt(punctuationPoint);
            tapWord(punctuationPoint);
            require(visible("rrAnnotationHighlightWord") && !visible("rrAnnotationEnd"),
                    "Same punctuation-adjacent word did not offer Highlight word alone");
            capture("punctuation-word-choice", "rrAnnotationPopover");
            tap("rrAnnotationCancel");
            const auto fullSentence = find("Alice was beginning");
            auto sentenceRects = std::make_shared<QVector<QRect>>();
            auto sentenceDone = std::make_shared<bool>(false);
            host.session()->rangeRects(fullSentence.start, fullSentence.end,
                                       [sentenceRects, sentenceDone](QVector<QRect> rectangles) {
                                           *sentenceRects = std::move(rectangles);
                                           *sentenceDone = true;
                                       });
            waitFor(
                [&] {
                    return *sentenceDone;
                },
                "Sentence start geometry timed out");
            require(!sentenceRects->isEmpty(), "Sentence start has no geometry");
            startAt(QPoint(sentenceRects->first().left() + ui::px(4), sentenceRects->first().center().y()));
            find(QString::fromUtf8("conversations?”"));
            tapWord(punctuationPoint);
            require(visible("rrAnnotationEnd") && !visible("rrAnnotationEndAfterPunctuation") &&
                        !visible("rrAnnotationStartBeforePunctuation"),
                    "Punctuation word did not use ordinary boundary actions");
            // Resolve the range preview's second geometry job before capture.
            waitForSavedPaint();
            capture("end-candidate-preview", "rrAnnotationPopover");
            tap("rrAnnotationDismiss");
            capture("end-candidate-dismissed", "rrAnnotationHeader");
            require(savedHighlights().isEmpty(), "Candidate preview persisted an unconfirmed range");
            tapWord(punctuationPoint);
            waitForSavedPaint();
            capture("punctuation-end-choice", "rrAnnotationPopover");
            chooseBoundary("rrAnnotationEnd");
            capture("punctuation-saved", "rrAnnotationHeader");
            waitFor(
                [&] {
                    return savedHighlights().size() == 1;
                },
                "Automatic punctuation passage did not persist");
            const auto automaticText = savedHighlights().first().toObject().value("text").toString();
            require(automaticText.startsWith("Alice was beginning") &&
                        automaticText.endsWith(QString::fromUtf8("conversations?”")),
                    "Ordinary End did not save the full sentence and trailing punctuation");
            auto *punctuationPage = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(
                punctuationPage->mapTo(canvas->surface(), punctuationPoint + QPoint(0, punctuationPage->topHeight())),
                punctuationPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationRemove");
                },
                "Saved punctuation word did not open actions");
            tap("rrAnnotationRemove");
            tap("rrConfirmRemoveAnnotation");
            waitFor(
                [&] {
                    return savedHighlights().isEmpty();
                },
                "Removing punctuation passage kept its stored entry");
            const auto quote = find("Curiouser and curiouser");
            sentenceRects->clear();
            *sentenceDone = false;
            host.session()->rangeRects(quote.start, quote.end,
                                       [sentenceRects, sentenceDone](QVector<QRect> rectangles) {
                                           *sentenceRects = std::move(rectangles);
                                           *sentenceDone = true;
                                       });
            waitFor(
                [&] {
                    return *sentenceDone;
                },
                "Quoted sentence start geometry timed out");
            require(!sentenceRects->isEmpty(), "Quoted sentence start has no geometry");
            startAt(QPoint(sentenceRects->first().left() + ui::px(4), sentenceRects->first().center().y()));
            require(!visible("rrAnnotationStartBeforePunctuation") && !visible("rrAnnotationEndAfterPunctuation"),
                    "Quoted word exposed a manual punctuation action");
            capture("automatic-quote-start", "rrAnnotationHeader");
            const QPoint quoteEndPoint = selection(find("and curiouser!"));
            tapWord(quoteEndPoint);
            chooseBoundary("rrAnnotationEnd");
            capture("automatic-quote-saved", "rrAnnotationHeader");
            waitFor(
                [&] {
                    return savedHighlights().size() == 1;
                },
                "Quoted sentence did not persist");
            require(savedHighlights().first().toObject().value("text").toString() ==
                        QString::fromUtf8("“Curiouser and curiouser!”"),
                    "Ordinary Start and End did not save paired quotation punctuation");
            const auto quotedSaved = savedHighlights();
            auto *savedPage = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(savedPage->mapTo(canvas->surface(),
                                     sentenceRects->first().center() + QPoint(0, savedPage->topHeight())),
                    savedPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationEdit");
                },
                "Saved highlight actions did not open");
            require(visible("rrAnnotationEdit") && visible("rrAnnotationStop"),
                    "Saved highlight menu has no edit or Stop action");
            tap("rrAnnotationStop");
            require(!visible("rrAnnotationPopover") && savedHighlights() == quotedSaved,
                    "Stopping highlighting changed the saved passage");
            menu();
            tap("rrSettings");
            tap("rrSettingsControls");
            qobject_cast<QCheckBox *>(widget("rrMinimalAnnotationMode"))->setChecked(true);
            tap("rrSettings");
            readingPage = host.window()->findChild<ReadingPage *>("rrPage");
            const auto minimalImage = readingPage->image;
            const auto minimalPosition = host.session()->state().value("position");
            tap("rrAnnotate");
            require(!visible("rrAnnotationHeader") && readingPage->image == minimalImage &&
                        host.session()->state().value("position") == minimalPosition,
                    "Minimal annotation mode changed the page or showed its bar");
            capture("minimal-mode", "rrPage");
            clickAt(readingPage->mapTo(canvas->surface(), QPoint(0, readingPage->height() / 2)), readingPage);
            waitFor(
                [&] {
                    return visible("rrAnnotationStop");
                },
                "Blank space did not offer Stop highlighting");
            require(visible("rrAnnotationDismiss") && !visible("rrAnnotationStart"),
                    "Blank annotation menu has incorrect actions");
            capture("minimal-blank-exit", "rrAnnotationPopover");
            tap("rrAnnotationStop");
            require(!visible("rrAnnotationPopover") && savedHighlights() == quotedSaved,
                    "Minimal Stop changed saved highlights or kept its menu");
            menu();
            return;
        }
        if (group == "bookmarks") {
            tap("rrBookmarks");
            capture("empty", "rrBookmarkList");
            tap("rrBookmarkListClose");
            pageTap();
            auto *page = host.window()->findChild<ReadingPage *>("rrPage");
            clickAt(page->mapTo(canvas->surface(), page->bookmarkCorner().center()), page);
            settled();
            capture("ribbon");
            reloadCreatedFixtures();
            menu();
            tap("rrBookmarks");
            waitFor(
                [&] {
                    return visible("rrBookmark0");
                },
                "Bookmark was not added");
            capture("list", "rrBookmarkList");
            tap("rrBookmarkRemove0");
            capture("remove-confirmation", "rrRemoveBookmark");
            auto savedCount = [&] {
                QDirIterator files(paths.annotations(), {"bookmarks.json"}, QDir::Files, QDirIterator::Subdirectories);
                int count = 0, stores = 0;
                while (files.hasNext()) {
                    const auto saved = storage::readJsonFile(files.next(), 1024 * 1024);
                    require(saved.status == storage::ReadStatus::Ok, "Saved bookmarks are unreadable");
                    count += saved.object.value("bookmarks").toArray().size();
                    ++stores;
                }
                require(stores == 1, "Bookmark tour did not create exactly one isolated store");
                return count;
            };
            require(savedCount() == 1, "Added bookmark was not persisted");
            tap("rrKeepBookmark");
            require(!visible("rrRemoveBookmark") && visible("rrBookmark0") && savedCount() == 1,
                    "Canceling removal changed the bookmark");
            tap("rrBookmarkRemove0");
            tap("rrConfirmRemoveBookmark");
            waitFor(
                [&] {
                    return !visible("rrRemoveBookmark") && !visible("rrBookmark0");
                },
                "Confirmed bookmark removal did not refresh the list");
            require(savedCount() == 0, "Removed bookmark remained in persistent storage");
            tap("rrBookmarkListClose");
            menu();
            tap("rrBookmarks");
            require(!visible("rrBookmark0"), "Removed bookmark returned when reopening the list");
            return;
        }
        if (group == "about") {
            tap("rrSettings");
            tap("rrSettingsAbout");
            tap("rrOpenAcknowledgments");
            capture("acknowledgments", "rrAcknowledgments");
            tap("rrAcknowledgment0");
            capture("license", "rrLicenseText");
            auto *license = dynamic_cast<LicenseText *>(widget("rrLicenseText"));
            require(license && license->pages() > 1 && license->page == 0, "License fixture did not paginate");
            const auto firstLicense = license->grab().toImage();
            const int licenseTurns = checkPagerEnds("rrAcknowledgments", license);
            require(licenseTurns == license->pages() - 1 && license->page == 0 &&
                        license->grab().toImage() == firstLicense,
                    "License paging did not restore the first content");
            tap("rrAcknowledgmentsBack");
            checkPagerEnds("rrAcknowledgments");
            return;
        }
        if (group == "dialogs") {
            pageTap();
            host.session()->go(host.session()->pages() - 1);
            settled();
            auto *page = widget("rrPage");
            QTest::mouseClick(page, Qt::LeftButton, Qt::NoModifier, {size.width() - 80, size.height() / 2});
            waitFor(
                [&] {
                    return visible("rrFinishBook");
                },
                "Finish prompt did not open");
            capture("finish-book", "rrFinishBook");
            tap("rrStillReading");
            settled();
            auto opening = createEmbeddedOpening(host.window(), test::makeHostPresentation);
            opening->window()->resize(size);
            opening->showFallback(OpeningFallback::Failure);
            opening->window()->show();
            capture("book-cannot-open", "rrOpeningFailure");
            return;
        }
        throw std::runtime_error("Unknown group: " + group.toStdString());
    }

private:
    bool dictionaryManagementFixture = false;
    bool pageButtonsSupported = true;
    QString output, group, theme, bookPath, fixtureDate;
    bool createdFixturesReloaded = false;
    QSize size;
    QTemporaryDir temporary;
    storage::Paths paths;
    test::ReaderHost host;
    std::unique_ptr<test::ReaderCanvas> canvas;
    std::shared_ptr<LookupState> lookup;
    std::function<void(bool)> bluetoothChanged;
};
}
int main(int argc, char **argv) {
    test::configureHostPlatform(argc, argv, true);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setCursorFlashTime(0);
    test::applyHostStyle();
    QCommandLineParser parser;
    parser.addHelpOption();
    for (const auto &name : {"book", "output", "group", "theme", "width", "height", "only", "ppi", "fixture-date"})
        parser.addOption(QCommandLineOption(name, name, "value"));
    parser.process(app);
    qputenv("KOBALT_SCREENSHOT_ONLY", parser.value("only").toUtf8());
    AssetBundle assets(QStringLiteral(KOBALT_ASSET_ROOT), QStringLiteral(KOBALT_VERSION));
    FontCopies copies;
    QTemporaryDir systemFonts, sideloadFonts;
    QTimer::singleShot(0, &app, [&] {
        try {
            require(assets.available() && ui::initializeInterfaceFonts(assets), "Font assets unavailable");
            for (const auto &file : bundledFontFiles(assets))
                QFontDatabase::addApplicationFont(file);
            auto fonts = engineFontInputs(assets, {}, {}, {});
            if (parser.value("group") == "typography") {
                require(systemFonts.isValid() && sideloadFonts.isValid(), "Cannot create isolated font directories");
                // Use real bundled faces outside the recommended collection to exercise each source.
                require(QFile::copy(assets.file("fonts/NotoSerif-Regular.ttf"),
                                    systemFonts.filePath("NotoSerif-Regular.ttf")),
                        "Cannot copy built-in font fixture");
                require(QFile::copy(assets.file("fonts/InstrumentSerif-Regular.ttf"),
                                    sideloadFonts.filePath("InstrumentSerif-Regular.ttf")),
                        "Cannot copy sideloaded font fixture");
                fonts.sources.systemDirectory = systemFonts.path();
                fonts.sources.sideloadDirectory = sideloadFonts.path();
                const auto catalogue = discoverReadingFonts(fonts.sources);
                bool builtIn = false, sideloaded = false;
                for (const auto &font : catalogue) {
                    builtIn |= font.origin == FontOrigin::BuiltIn && font.family == "Noto Serif";
                    sideloaded |= font.origin == FontOrigin::Sideloaded && font.family == "Instrument Serif";
                }
                require(builtIn && sideloaded, "Font fixture sources were not discovered");
            }
            const QString fixtureDate =
                parser.isSet("fixture-date") ? parser.value("fixture-date") : "2000-01-01T12:00:00Z";
            const auto timestamp = QDateTime::fromString(fixtureDate, Qt::ISODate);
            require(timestamp.isValid() && timestamp.toUTC().toString(Qt::ISODate) == fixtureDate,
                    "Fixture date must be a canonical UTC timestamp");
            bool densityValid = true;
            const int ppi = parser.isSet("ppi") ? parser.value("ppi").toInt(&densityValid) : 300;
            require(densityValid && ppi > 0, "Density must be positive");
            ui::setDensity(ppi);
            readingPpi.store(ppi);
            QApplication::setFont(ui::interfaceFont());
            {
                Tour tour(parser.value("book"), parser.value("output"), parser.value("group"), parser.value("theme"),
                          fonts, placeholderFontFamily(assets, copies),
                          {parser.value("width").toInt(), parser.value("height").toInt()}, fixtureDate);
                tour.run();
            }
            app.exit(0);
        } catch (const std::exception &error) {
            std::fprintf(stderr, "CAPTURE FAILED: %s\n", error.what());
            app.exit(1);
        }
    });
    return app.exec();
}
