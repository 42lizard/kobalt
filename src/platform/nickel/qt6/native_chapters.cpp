#include "support/qt_compat.h"
#include "support/strings.h"
#include "platform/nickel/common/native_chapters.h"
#include "engine/crengine/engine_activity.h"
#include "support/diagnostics.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMetaType>
#include <QPointer>
#include <QThread>
#include <QVariantMap>
#include <atomic>
#include <cstring>
#include <mutex>
#include <dlfcn.h>
#ifndef KOBALT_READER_HARDWARE
#include <QApplication>
#include <QWidget>
#include <QFile>
#include <QJsonArray>
#include "storage/reader_storage.h"
#endif

namespace kobalt {
namespace {
template <class T> T symbol(const char *name) {
    return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
}
struct Volume {
    void *words[3];
    Volume(const Volume &) = delete;
    ~Volume() {
        symbol<void (*)(void *)>("_ZN6VolumeD1Ev")(this);
    }
};
struct Location {
    void *words[2];
    Location(const Location &) = delete;
    ~Location() {
        symbol<void (*)(void *)>("_ZN15ContentLocationD1Ev")(this);
    }
};
struct Shortcovers {
    void *allocation;
    const char *entries;
    int count;
    Shortcovers(const Shortcovers &) = delete;
    ~Shortcovers() {
        symbol<void (*)(void *)>("_ZN17QArrayDataPointerI10ShortcoverED1Ev")(this);
    }
    const void *at(int index) const {
        return entries + index * 168;
    }
};
static_assert(sizeof(Volume) == 12 && sizeof(Shortcovers) == 12 && sizeof(Location) == 8);
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
        symbol<decltype(book)>("_ZN17ShortcoverManager16retrieveByBookIdERK7QStringRK5QListI10ShortcoverES2_");
    Shortcovers (*spine)(const QString &, const Shortcovers &, const QString &) = symbol<decltype(spine)>(
        "_ZN17ShortcoverManager36retrieveSpineByBookIdWithTOCFallbackERK7QStringRK5QListI10ShortcoverES2_");
    Shortcovers (*toc)(const QString &, const Shortcovers &, const Shortcovers &, const QString &) =
        symbol<decltype(toc)>(
            "_ZN17ShortcoverManager36retrieveTOCByBookIdWithSpineFallbackERK7QStringRK5QListI10ShortcoverES7_S2_");
    Shortcovers (*merge)(const Shortcovers &, const Shortcovers &) =
        symbol<decltype(merge)>("_ZN17ShortcoverManager25retrieveMergedSpineAndTOCERK5QListI10ShortcoverES4_");
    QVariantMap (*chapterValues)(const void *) = symbol<decltype(chapterValues)>("_ZNK10Shortcover11getDbValuesEv");
    QString (*title)(const void *) = symbol<decltype(title)>("_ZNK7Content8getTitleEv");
    Location (*location)(const void *) = symbol<decltype(location)>("_ZNK10Shortcover11getLocationEv");
    QString (*teng)(const Location *) = symbol<decltype(teng)>("_ZNK15ContentLocation12toTENGStringEv");
    int (*depth)(const void *) = symbol<decltype(depth)>("_ZNK10Shortcover8tocDepthEv");
    bool (*locked)(const void *) = symbol<decltype(locked)>("_ZNK10Shortcover10isKoboLockEv");
    void (*construct)(void *, QObject *, const Volume &, const Shortcovers &, const void *) =
        symbol<decltype(construct)>("_ZN20KepubBookWordCounterC1EP7QObjectRK6VolumeRK5QListI10ShortcoverERK6Device");
    QList<int> (*count)(void *) = symbol<decltype(count)>("_ZN20KepubBookWordCounter11wordsInBookEv");
    void (*abort)(void *) = symbol<decltype(abort)>("_ZN20KepubBookWordCounter5abortEv");
    QMetaType chapter{symbol<const QtPrivate::QMetaTypeInterface *>(
        "_ZN9QtPrivate25QMetaTypeInterfaceWrapperI10ShortcoverE8metaTypeE")};
    QMetaType counter{symbol<const QtPrivate::QMetaTypeInterface *>(
        "_ZN9QtPrivate25QMetaTypeInterfaceWrapperI20KepubBookWordCounterE8metaTypeE")};
    bool available() const {
        return volume && values && device && dbName && closeDb && all && book && spine && toc && merge &&
               chapterValues && title && location && teng && depth && locked && construct && count && abort &&
               chapter.sizeOf() == 168 && counter.sizeOf() == 48 && symbol<void *>("_ZN15ContentLocationD1Ev") &&
               symbol<void *>("_ZN6VolumeD1Ev") && symbol<void *>("_ZN17QArrayDataPointerI10ShortcoverED1Ev");
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
    const auto location = a.location(entry);
    return {a.title(entry), a.teng(&location), a.depth(entry), 0};
}
}
struct NativeChapters::Job {
    std::atomic_bool cancelled{false};
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
        if (!a.available()) {
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
        if (merged.count < 0 || merged.count > 65536 || toc.count < 0 || toc.count > 65536 || spine.count < 0 ||
            spine.count > 65536) {
            result.error = strings::get("error.native_chapter_list_exceeds_its_limit");
            return result;
        }
        const auto &contents = toc.count ? toc : spine;
        for (int i = 0; i < contents.count; ++i)
            if (!a.locked(contents.at(i)))
                result.contents.append(chapter(contents.at(i)));
        for (int i = 0; i < merged.count; ++i)
            if (!a.locked(merged.at(i)))
                result.graph.append(chapter(merged.at(i)));
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancelled.load())
                return {};
            auto *storage = ::operator new(48);
            a.construct(storage, nullptr, volume, merged, a.device());
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
        const auto counts = cancelled.load() ? QList<int>() : a.count(counter);
        {
            std::lock_guard<std::mutex> lock(mutex);
            delete counter;
            counter = nullptr;
        }
        if (cancelled.load())
            return {};
        result.nativeEntries = merged.count;
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
        QVector<ChapterTarget> spineTargets;
        for (int i = 0; i < spine.count; ++i)
            if (!a.locked(spine.at(i)))
                spineTargets.append(chapter(spine.at(i)));
        result.totalWords = spineWordCount(spineTargets, result.graph);
        // Nickel's counter needs the merged input, but its spine entries are
        // internal files, not extra chapters. Match by identity: a spine and TOC
        // entry can share a destination but cover different amounts of text.
        QMap<QString, int> words;
        for (int i = 0; i < merged.count; ++i)
            words.insert(a.chapterValues(merged.at(i)).value("ContentID").toString(), counts[i]);
        result.graph.clear();
        for (int i = 0; i < contents.count; ++i) {
            if (a.locked(contents.at(i)))
                continue;
            const auto id = a.chapterValues(contents.at(i)).value("ContentID").toString();
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
            diagnostic("chapter-count", {{"cancelled", work->cancelled.load()},
                                         {"ready", result.ready},
                                         {"chapters", result.graph.size()},
                                         {"entries", result.nativeEntries},
                                         {"counts", result.nativeCounts},
                                         {"milliseconds", timer.elapsed()}});
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
void NativeChapters::testCommand() {
    QFile request("/instance/native-chapter-state");
    if (!request.exists())
        return;
    QJsonObject result;
    for (auto *widget : QApplication::allWidgets()) {
        if (QByteArray(widget->metaObject()->className()) != "ReadingView")
            continue;
        if (!symbol<bool (*)(void *)>("_ZNK11ReadingView15wordCountsReadyEv")(widget))
            continue;
        const auto counts = symbol<QList<int> (*)(void *)>("_ZN11ReadingView10wordCountsEv")(widget);
        QJsonArray words;
        for (int value : counts)
            words.append(value);
        result["words"] = words;
        result["current"] = symbol<int (*)(void *)>("_ZN11ReadingView22mergedTOCAndSpineIndexEv")(widget);
        result["page"] = symbol<int (*)(void *)>("_ZN11ReadingView18chapterCurrentPageEv")(widget);
        result["pages"] = symbol<int (*)(void *)>("_ZN11ReadingView17chapterTotalPagesEv")(widget);
        // The supported reader layout stores the word-counter input at private+152.
        // Read it only for simulator comparison, never to drive Kobalt.
        const char *details = nullptr;
        std::memcpy(&details, reinterpret_cast<const char *>(widget) + 144, sizeof(details));
        const auto *entries = reinterpret_cast<const Shortcovers *>(details + 152);
        QJsonArray locations, titles;
        if (entries->count >= 0 && entries->count <= 65536)
            for (int i = 0; i < entries->count; ++i) {
                const auto value = chapter(entries->at(i));
                locations.append(value.location);
                titles.append(value.title);
            }
        result["locations"] = locations;
        result["titles"] = titles;
    }
    storage::writeJson("/instance/artifacts/native-chapter-state.json", result);
    request.remove();
}
#endif
}
