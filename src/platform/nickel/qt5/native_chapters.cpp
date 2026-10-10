#include "support/qt_compat.h"
#include "platform/nickel/common/publication_source.h"
#include "support/strings.h"
#include "platform/nickel/common/native_chapters.h"
#include "engine/crengine/engine_activity.h"
#include "support/diagnostics.h"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QVariantMap>
#include <atomic>
#include <cstring>
#include <mutex>
#include <dlfcn.h>
#ifndef KOBALT_READER_HARDWARE
#include <QFile>
#include <QWidget>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QApplication>
#endif

namespace kobalt {
namespace {
template <class T> T symbol(const char *name) {
    return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
}
struct Volume {
    void *words[2];
    Volume(const Volume &) = delete;
    ~Volume() {
        symbol<void (*)(void *)>("_ZN6VolumeD1Ev")(this);
    }
};
// Firmware 4 uses 32-byte or 48-byte Shortcover objects. Let Nickel's
// QVector accessor copy the entry so its own layout determines the stride.
// The return buffer accommodates both layouts; Nickel owns destruction.
struct Shortcover {
    void *words[12];
    Shortcover(const Shortcover &) = delete;
    ~Shortcover() {
        symbol<void (*)(void *)>("_ZN10ShortcoverD1Ev")(this);
    }
    operator const void *() const {
        return this;
    }
};
struct Shortcovers {
    const char *allocation;
    Shortcovers(const Shortcovers &) = delete;
    ~Shortcovers() {
        symbol<void (*)(void *)>("_ZN7QVectorI10ShortcoverED1Ev")(this);
    }
    int count() const {
        int value;
        std::memcpy(&value, allocation + 4, sizeof(value));
        return value;
    }
    Shortcover at(int index) const {
        //nb lookup libnickel 4.38.21908 4.99.99999 _ZNK7QVectorI10ShortcoverE5valueEi
        return symbol<Shortcover (*)(const Shortcovers *, int)>("_ZNK7QVectorI10ShortcoverE5valueEi")(this, index);
    }
};
static_assert(sizeof(Volume) == 8 && sizeof(Shortcover) == 48 && sizeof(Shortcovers) == 4);
struct Api {
    Volume (*volume)(const QString &,
                     const QString &) = symbol<decltype(volume)>("_ZN13VolumeManager7getByIdERK7QStringS2_");
    QVariantMap (*values)(const void *) = symbol<decltype(values)>("_ZNK6Volume11getDbValuesEv");
    const void *(*device)() = symbol<decltype(device)>("_ZN6Device16getCurrentDeviceEv");
    const QString &(*dbName)(const void *) = symbol<decltype(dbName)>("_ZNK6Device9getDbNameEv");
    void (*closeDb)(QString) = symbol<decltype(closeDb)>("_ZN15DatabaseAdapter15closeConnectionE7QString");
    Shortcovers (*all)(const QString &, const QString &) =
        symbol<decltype(all)>("_ZN17ShortcoverManager28retrieveByBookIdIncludingTOCERK7QStringS2_");
    Shortcovers (*book)(const QString &, const Shortcovers &, const QString &) =
        symbol<decltype(book)>("_ZN17ShortcoverManager16retrieveByBookIdERK7QStringRK7QVectorI10ShortcoverES2_");
    Shortcovers (*spine)(const QString &, const Shortcovers &, const QString &) = symbol<decltype(spine)>(
        "_ZN17ShortcoverManager36retrieveSpineByBookIdWithTOCFallbackERK7QStringRK7QVectorI10ShortcoverES2_");
    Shortcovers (*toc)(const QString &, const Shortcovers &, const Shortcovers &, const QString &) =
        symbol<decltype(toc)>(
            "_ZN17ShortcoverManager36retrieveTOCByBookIdWithSpineFallbackERK7QStringRK7QVectorI10ShortcoverES7_S2_");
    Shortcovers (*merge)(const Shortcovers &, const Shortcovers &) =
        symbol<decltype(merge)>("_ZN17ShortcoverManager25retrieveMergedSpineAndTOCERK7QVectorI10ShortcoverES4_");
    QVariantMap (*chapterValues)(const void *) = symbol<decltype(chapterValues)>("_ZNK10Shortcover11getDbValuesEv");
    QString (*title)(const void *) = symbol<decltype(title)>("_ZNK7Content8getTitleEv");
    QString (*id)(const void *) = symbol<decltype(id)>("_ZNK10Shortcover5getIdEv");
    void (*parse)(QString, QString *, QString *, QString *,
                  QString *) = symbol<decltype(parse)>("_ZN10EPubParser17parseShortcoverIdE7QStringPS0_S1_S1_S1_");
    bool (*locked)(const void *) = symbol<decltype(locked)>("_ZNK10Shortcover10isKoboLockEv");
    void (*construct)(void *, const Volume &, const Shortcovers &, const void *) =
        symbol<decltype(construct)>("_ZN20KepubBookWordCounterC1ERK6VolumeRK7QVectorI10ShortcoverERK6Device");
    QVector<int> (*count)(void *) = symbol<decltype(count)>("_ZN20KepubBookWordCounter11wordsInBookEv");
    void (*abort)(void *) = symbol<decltype(abort)>("_ZN20KepubBookWordCounter5abortEv");
    bool available() const {
        return volume && values && device && dbName && closeDb && all && book && spine && toc && merge &&
               chapterValues && title && id && parse && locked && construct && count && abort &&
               symbol<void *>("_ZN6VolumeD1Ev") && symbol<void *>("_ZN7QVectorI10ShortcoverED1Ev") &&
               symbol<void *>("_ZNK7QVectorI10ShortcoverE5valueEi") && symbol<void *>("_ZN10ShortcoverD1Ev");
    }
};
const Api &api() {
    static const Api value;
    return value;
}
QObject *worker() {
    static auto *object = [] {
        auto *thread = new QThread(qApp);
        auto *object = new QObject;
        object->moveToThread(thread);
        QObject::connect(thread, &QThread::finished, object, &QObject::deleteLater);
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread] {
            engineActivity().pause(false);
            thread->quit();
            thread->wait();
        });
        thread->start();
        return object;
    }();
    return object;
}
ChapterTarget chapter(const void *entry) {
    const auto &a = api();
    QString book, file, directory, fragment;
    a.parse(a.id(entry), &book, &file, &directory, &fragment);
    const QString path = directory.isEmpty() ? file : directory + '/' + file;
    return {a.title(entry), path + (fragment.isEmpty() ? QString() : '#' + fragment),
            a.chapterValues(entry).value("Depth").toInt(), 0};
}
}
struct NativeChapters::Job {
    std::atomic_bool cancelled{false};
    bool supported = native::publicationSource().available();
    std::mutex mutex;
    QObject *counter = nullptr;
    void cancel() {
        cancelled.store(true);
        std::lock_guard<std::mutex> lock(mutex);
        // Nickel also calls abort from its GUI while the counter is running.
        // Hold ownership across that call so worker cleanup cannot delete it.
        if (counter)
            api().abort(counter);
    }
    NativeChapterData read(const QString &identity) {
        NativeChapterData result;
        const auto &a = api();
        if (!supported || !a.available()) {
            result.error = strings::get("error.native_chapter_data_is_unavailable");
            return result;
        }
        if (cancelled.load())
            return result;
        const QString database = a.dbName(a.device());
        // This worker owns its thread-local native connection. Close it even
        // on cancellation before the controller permits USB or power-off.
        struct Connection {
            QString name;
            ~Connection() {
                api().closeDb(name);
            }
        } connection{database};
        const auto volume = a.volume(identity, database);
        if (a.values(&volume).value("ContentID").toString() != identity) {
            result.error = strings::get("error.native_chapter_book_changed");
            return result;
        }
        // Match ReadingView::setupVolumeState, including its fallback lists.
        // Direct spine/TOC queries do not produce the same graph intervals.
        const auto all = a.all(identity, database);
        const auto book = a.book(identity, all, database);
        const auto spine = a.spine(identity, book, database);
        const auto toc = a.toc(identity, book, all, database);
        const auto merged = a.merge(spine, toc);
        if (merged.count() < 0 || merged.count() > 65536 || toc.count() < 0 || toc.count() > 65536 ||
            spine.count() < 0 || spine.count() > 65536) {
            result.error = strings::get("error.native_chapter_list_exceeds_its_limit");
            return result;
        }
        const auto &contents = toc.count() ? toc : spine;
        for (int i = 0; i < contents.count(); ++i)
            if (!a.locked(contents.at(i)))
                result.contents.append(chapter(contents.at(i)));
        for (int i = 0; i < merged.count(); ++i)
            if (!a.locked(merged.at(i)))
                result.graph.append(chapter(merged.at(i)));
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancelled.load())
                return {};
            auto *storage = ::operator new(36);
            a.construct(storage, volume, merged, a.device());
            counter = static_cast<QObject *>(storage);
        }
#ifndef KOBALT_READER_HARDWARE
        // Hold a real owned counter so the transition test can cancel it
        // before execution without depending on host speed or book size.
        if (QFile::exists("/instance/hold-chapter-count")) {
            QFile waiting("/instance/chapter-count-waiting");
            waiting.open(QIODevice::WriteOnly);
            waiting.close();
            while (!cancelled.load() && QFile::exists("/instance/hold-chapter-count"))
                QThread::msleep(10);
            waiting.remove();
        }
#endif
        const auto counts = cancelled.load() ? QVector<int>() : a.count(counter);
        {
            std::lock_guard<std::mutex> lock(mutex);
            delete counter;
            counter = nullptr;
        }
        if (cancelled.load())
            return {};
        result.nativeEntries = merged.count();
        result.nativeCounts = counts.size();
        // Nickel returns no counts for books it does not count, such as plain EPUB files.
        if (counts.isEmpty() && !result.graph.isEmpty()) {
            result.error = strings::get("error.native_chapter_count_is_unavailable");
            return result;
        }
        if (counts.size() != result.graph.size()) {
            result.error = strings::get("error.native_chapter_counts_do_not_match_the_chapter_list");
            return result;
        }
        for (int i = 0; i < counts.size(); ++i) {
            if (counts[i] < 0) {
                result.error = strings::get("error.native_chapter_count_is_unavailable");
                return result;
            }
            result.graph[i].words = counts[i];
        }
        // Firmware 4's merged list splits a spine file at each TOC fragment, and
        // the counter returns the words between consecutive entries. The
        // intervals cover the book once, so their sum is the total. Taking one
        // count per spine file, as firmware 5 does, would drop the later parts.
        result.totalWords = 0;
        for (int i = 0; i < merged.count(); ++i)
            if (!a.locked(merged.at(i)))
                result.totalWords += counts[i];
        // Firmware 4 adds a suffix to duplicate TOC database IDs. Its
        // Shortcover::getId removes that suffix, as the native merger does.
        // Use that native identity to match TOC entries to counter intervals.
        QMap<QString, int> words;
        for (int i = 0; i < merged.count(); ++i) {
            const auto id = a.id(merged.at(i));
            if (id.isEmpty() || (words.contains(id) && words.value(id) != counts[i])) {
                result.error = strings::get("error.native_chapter_count_is_ambiguous");
                return result;
            }
            words.insert(id, counts[i]);
        }
        result.graph.clear();
        for (int i = 0; i < contents.count(); ++i) {
            if (a.locked(contents.at(i)))
                continue;
            const auto id = a.id(contents.at(i));
            if (id.isEmpty() || !words.contains(id)) {
                result.error = strings::get("error.native_chapter_count_is_unavailable");
                return result;
            }
            auto entry = chapter(contents.at(i));
            entry.words = words.value(id);
            result.graph.append(entry);
        }
        result.ready = true;
        return result;
    }
};
NativeChapters::NativeChapters(QString identity) : job(std::make_shared<Job>()) {
    QPointer<NativeChapters> guard(this);
    const auto work = job;
    kobalt::qt::invoke(
        worker(),
        [guard, work, identity] {
            EngineJob activity;
            QElapsedTimer timer;
            timer.start();
            auto result = work->read(identity);
            diagnostic("chapter-count", kobalt::qt::object({{"cancelled", work->cancelled.load()},
                                                            {"ready", result.ready},
                                                            {"chapters", result.graph.size()},
                                                            {"entries", result.nativeEntries},
                                                            {"counts", result.nativeCounts},
                                                            {"milliseconds", timer.elapsed()}}));
            kobalt::qt::invoke(
                qApp,
                [guard, result = std::move(result)] {
                    if (!guard)
                        return;
                    guard->result = result;
                    if (guard->updated)
                        guard->updated();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
NativeChapters::~NativeChapters() {
    job->cancel();
}
void NativeChapters::drain(std::function<void()> finished) {
    kobalt::qt::invoke(
        worker(),
        [finished] {
            kobalt::qt::invoke(qApp, finished, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
#ifndef KOBALT_READER_HARDWARE
// Simulator comparison with Nickel's own reader. The word counts, current entry
// and chapter pages come from exported ReadingView functions. Firmware 5 also
// reads the counter's input list from private ReadingView fields; those offsets
// are not inspected on firmware 4, so the list is rebuilt with the same native
// merge calls for the one generated fixture. Never inspect an arbitrary book.
void NativeChapters::testCommand() {
    QFile request("/instance/native-chapter-state");
    if (!request.exists())
        return;
    QJsonObject result;
    const QString fixture = "file:///mnt/onboard/chapter-counts.kepub.epub";
    const auto ready = symbol<bool (*)(void *)>("_ZNK11ReadingView15wordCountsReadyEv");
    const auto wordCounts = symbol<QVector<int> (*)(void *)>("_ZN11ReadingView10wordCountsEv");
    const auto current = symbol<int (*)(void *)>("_ZN11ReadingView22mergedTOCAndSpineIndexEv");
    const auto page = symbol<int (*)(void *)>("_ZN11ReadingView18chapterCurrentPageEv");
    const auto pages = symbol<int (*)(void *)>("_ZN11ReadingView17chapterTotalPagesEv");
    const auto &a = api();
    if (ready && wordCounts && current && page && pages && a.available() && native::publicationSource().available()) {
        for (auto *widget : QApplication::allWidgets()) {
            if (QByteArray(widget->metaObject()->className()) != "ReadingView" || !ready(widget))
                continue;
            QJsonArray words;
            for (int value : wordCounts(widget))
                words.append(value);
            result["words"] = words;
            result["current"] = current(widget);
            result["page"] = page(widget);
            result["pages"] = pages(widget);
            const QString database = a.dbName(a.device());
            const auto all = a.all(fixture, database);
            const auto book = a.book(fixture, all, database);
            const auto spine = a.spine(fixture, book, database);
            const auto toc = a.toc(fixture, book, all, database);
            const auto merged = a.merge(spine, toc);
            QJsonArray locations, titles;
            if (merged.count() >= 0 && merged.count() <= 65536)
                for (int i = 0; i < merged.count(); ++i) {
                    const auto value = chapter(merged.at(i));
                    locations.append(value.location);
                    titles.append(value.title);
                }
            result["locations"] = locations;
            result["titles"] = titles;
        }
    }
    QFile output("/instance/artifacts/native-chapter-state.json");
    if (output.open(QIODevice::WriteOnly))
        output.write(QJsonDocument(result).toJson());
    request.remove();
}
#endif
}
