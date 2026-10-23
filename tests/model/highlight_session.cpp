#include "check.h"
#include "interfaces/opening_message.h"
#include "publication/native_archive.h"
#include "publication/native_resources.h"
#include "publication/resource_server.h"
#include "reader_inputs.h"
#include "reading/bookmark_session.h"
#include "reading/highlight_session.h"
#include "reading/reader_session.h"
#include "support/qt_compat.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTimer>

using namespace kobalt;
namespace {
bool samePassage(const Highlight &a, const Highlight &b) {
    return a.id == b.id && a.created == b.created && a.start == b.start && a.end == b.end && a.text == b.text &&
           a.chapter == b.chapter && a.chosenStart == b.chosenStart && a.chosenEnd == b.chosenEnd &&
           a.sentenceStartPolicy == b.sentenceStartPolicy && a.sentenceEndPolicy == b.sentenceEndPolicy;
}
void waitUntil(const std::function<bool()> &ready) {
    if (ready()) {
        return;
    }
    QEventLoop loop;
    QTimer poll, timeout;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [] {
        require(false, "HighlightSession check timed out");
    });
    poll.start(1);
    timeout.start(60000);
    loop.exec();
}

}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 2, "Expected a synthetic EPUB");
    const auto path = QString::fromLocal8Bit(argv[1]);
    native::ResourceServer server;
    require(server.open(path,
                        [path] {
                            return native::openPlainArchive(path);
                        }),
            "Cannot open synthetic publication");
    QTemporaryDir annotations;
    require(annotations.isValid(), "Cannot create annotation directory");
    // Run inside the event loop, so quitting stops the engine worker as the
    // application does.
    qt::later(0, &app, [&] {
        auto owned = std::make_unique<ReaderSession>(test::readerFonts(), test::readerPlaceholder());
        auto &model = *owned;
        auto lease = std::make_shared<SourceLease>();
        bool ready = false;
        model.open({"file:///highlight-session.epub",
                    path,
                    QSize(758, 900),
                    {},
                    {},
                    {},
                    lease,
                    nativeResourceFactory(path, server.port(), lease)},
                   [&](const OpeningResult &result) {
                       require(result.ready(), qPrintable(openingMessage(result)));
                       ready = true;
                   });
        waitUntil([&] {
            return ready && !model.busy();
        });
        auto layout = [&] {
            return bookmarkLayoutKey(model.pages(), model.state(), QSize(758, 900));
        };
        HighlightSession session(model, layout, annotations.path(), "file:///highlight-session.epub",
                                 model.sourceHash(), "Book");
        require(session.writable(), "New highlight session is not writable");
        bool searched = false;
        SearchHit first, last;
        model.search("the", [&](SearchBatch batch) {
            if (!batch.hits.isEmpty()) {
                if (first.start.isEmpty()) {
                    first = batch.hits.first();
                }
                last = batch.hits.last();
            }
            searched = batch.done;
        });
        waitUntil([&] {
            return searched;
        });
        require(!first.start.isEmpty(), "No source text fixture for highlight");
        model.cancelSearch();
        require(model.goTo(first.start), "Cannot show grouped geometry fixture");
        waitUntil([&] {
            return !model.busy();
        });
        bool geometryReady = false;
        model.rangeRectGroups(
            {{first.start, first.end}, {"bad", "bad"}, {first.start, first.end}}, [&](QVector<QVector<QRect>> groups) {
                require(groups.size() == 3 && !groups[0].isEmpty() && groups[1].isEmpty() && groups[0] == groups[2],
                        "Grouped geometry lost range order or clipping");
                geometryReady = true;
            });
        waitUntil([&] {
            return geometryReady;
        });
        {
            const auto identity = QStringLiteral("future-color-book");
            HighlightStore futureStore(annotations.path(), identity, model.sourceHash(), "Future");
            require(futureStore.load(), "Cannot initialize future-color fixture");
            Highlight future{"future", first.start, first.end, first.text, {}, QDateTime::currentDateTimeUtc()};
            future.color = "orange";
            futureStore.setHighlights({future});
            require(futureStore.save(), "Cannot store future color");
            HighlightSession futureSession(model, layout, annotations.path(), identity, model.sourceHash(), "Future");
            bool futureDone = false;
            futureSession.save(first.start, last.end, future.id, [&](bool ok) {
                require(ok, "Cannot edit future-color passage");
                futureDone = true;
            });
            waitUntil([&] {
                return futureDone;
            });
            require(futureSession.highlights().first().color == "orange" &&
                        highlightDisplayColor(futureSession.highlights().first().color) == "gray" &&
                        futureStore.load() && futureStore.highlights().first().color == "orange",
                    "Omitted edit color destroyed unknown stored key");
            require(futureSession.recolor(future.id, "gray") && futureSession.highlights().first().color == "gray" &&
                        futureStore.load() && futureStore.highlights().first().color == "gray",
                    "Displayed gray future color could not explicitly become gray");
        }
        bool completed = false, saved = false;
        session.save(first.start, last.end, {}, [&](bool ok) {
            saved = ok;
            completed = true;
        });
        waitUntil([&] {
            return completed;
        });
        require(saved && session.highlights().size() == 1, "Passage save failed");
        completed = false;
        session.save(
            first.start, last.end, session.highlights().first().id,
            [&](bool ok) {
                saved = ok;
                completed = true;
            },
            last.end, true, first.start, true);
        waitUntil([&] {
            return completed;
        });
        require(saved, "Cannot save automatic endpoint provenance");
        const auto beforeColor = session.highlights().first();
        require(beforeColor.color == "gray", "New omitted color was not gray");
        require(session.recolor(beforeColor.id, "blue") && session.highlights().first().color == "blue" &&
                    samePassage(beforeColor, session.highlights().first()),
                "Recolor changed passage metadata or failed");
        require(!session.recolor(beforeColor.id, "orange") && session.highlights().first().color == "blue",
                "Unsupported explicit color was accepted");
        const auto passage = session.highlights().first();
        completed = false;
        session.save(first.start, last.end, {}, [&](bool ok) {
            saved = ok;
            completed = true;
        });
        waitUntil([&] {
            return completed;
        });
        require(saved && session.highlights().size() == 2 && session.highlights()[0].id != session.highlights()[1].id,
                "Overlapping highlights were merged");
        require(model.style(64, 120), "Cannot reflow highlight fixture");
        waitUntil([&] {
            return !model.busy();
        });
        waitUntil([&] {
            session.sync();
            return session.resolved();
        });
        require(session.page(0) >= 0 && session.endPage(0) >= session.page(0) && session.highlights().size() == 2,
                "Reflow lost or merged highlights");
        int recolorChanges = 0;
        session.changed = [&] {
            ++recolorChanges;
        };
        require(session.recolor(passage.id, passage.color) && recolorChanges == 0 && session.resolved(),
                "Same-color recolor invalidated geometry or notified a change");
        session.changed = {};
        {
            HighlightSession reopened(model, layout, annotations.path(), "file:///highlight-session.epub",
                                      model.sourceHash(), "Book");
            require(reopened.highlights().first().color == "blue" && reopened.highlights().first().sentenceEndPolicy &&
                        reopened.highlights().first().chosenEnd == last.end &&
                        reopened.highlights().first().sentenceStartPolicy &&
                        reopened.highlights().first().chosenStart == first.start,
                    "Reopened session lost raw endpoint policy");
            require(reopened.highlights().size() == 2 && reopened.highlights()[0].text == passage.text,
                    "Reopened session lost full passage");
        }
        completed = false;
        session.save(first.start, first.end, passage.id, [&](bool ok) {
            saved = ok;
            completed = true;
        });
        waitUntil([&] {
            return completed;
        });
        require(saved && session.highlights().size() == 2 && session.highlights()[0].id == passage.id &&
                    session.highlights()[0].text == first.text && session.highlights()[0].color == "blue",
                "Editing changed passage identity or kept old text");
        require(session.remove(passage.id) && session.highlights().size() == 1, "Delete failed");
        completed = false;
        session.save("bad", first.end, {}, [&](bool ok) {
            saved = ok;
            completed = true;
        });
        waitUntil([&] {
            return completed;
        });
        require(!saved && session.highlights().size() == 1, "Invalid range changed saved passages");
        // A failed atomic replacement must preserve the saved passage and
        // remain retryable through the ordinary save API.
        const auto previous = session.highlights().first();
        const auto savedPath = annotations.path() + '/' +
                               BookmarkStore::bookKey("file:///highlight-session.epub", model.sourceHash()) +
                               "/highlights.json";
        const auto backup = savedPath + ".save-backup";
        auto readSaved = [&] {
            QFile file(savedPath);
            require(file.open(QIODevice::ReadOnly), "Cannot read saved highlight fixture");
            return file.readAll();
        };
        const auto savedBytes = readSaved();
        require(QFile::rename(savedPath, backup) && QDir().mkdir(savedPath),
                "Cannot block atomic highlight replacement");
        require(!session.recolor(previous.id, "pink") && samePassage(previous, session.highlights().first()) &&
                    session.highlights().first().color == previous.color,
                "Failed recolor changed live saved data");
        completed = false;
        session.save(
            first.start, first.end, previous.id,
            [&](bool ok) {
                saved = ok;
                completed = true;
            },
            first.end, true, first.start, true);
        waitUntil([&] {
            return completed;
        });
        require(!saved && session.highlights().size() == 1 && session.highlights().first().id == previous.id &&
                    session.highlights().first().start == previous.start &&
                    session.highlights().first().end == previous.end &&
                    session.highlights().first().text == previous.text &&
                    session.highlights().first().created == previous.created &&
                    session.highlights().first().chosenStart == previous.chosenStart &&
                    session.highlights().first().chosenEnd == previous.chosenEnd,
                "Failed edit changed live saved data");
        require(QDir().rmdir(savedPath) && QFile::rename(backup, savedPath), "Cannot restore atomic highlight fixture");
        require(readSaved() == savedBytes, "Failed edit replaced prior saved bytes");
        completed = false;
        session.save(
            first.start, first.end, previous.id,
            [&](bool ok) {
                saved = ok;
                completed = true;
            },
            first.end, true, first.start, true);
        waitUntil([&] {
            return completed;
        });
        require(saved && session.highlights().size() == 1 && session.highlights().first().id == previous.id &&
                    session.highlights().first().created == previous.created &&
                    session.highlights().first().text == first.text &&
                    session.highlights().first().sentenceStartPolicy && session.highlights().first().sentenceEndPolicy,
                "Retry failed or changed identity, creation date or punctuation provenance");
        completed = false;
        session.save(
            first.start, last.end, {},
            [&](bool ok) {
                saved = ok;
                completed = true;
            },
            {}, false, {}, false, "yellow");
        waitUntil([&] {
            return completed;
        });
        require(saved && session.highlights().last().color == "yellow", "Explicit create color was lost");
        require(session.recolor(previous.id, "pink") && session.highlights().first().color == "pink",
                "Failed recolor was not retryable");
        lease->revoke();
        owned.reset();
        ReaderSession::drain([&] {
            app.quit();
        });
    });
    return app.exec();
}
