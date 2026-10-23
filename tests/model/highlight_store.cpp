#include "storage/highlight_store.h"
#include "storage/bookmark_store.h"
#include "check.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <utime.h>

using namespace kobalt;
namespace {
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read fixture");
    return file.readAll();
}
void replace(const QString &path, const QByteArray &data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size(),
            "Cannot write fixture");
}
}
int main() {
    QTemporaryDir root;
    require(root.isValid(), "No temporary folder");
    const QByteArray fingerprint(64, 'a');
    HighlightStore store(root.path(), "file:///book.epub", fingerprint, "Book");
    require(store.load(), "Cannot load empty highlights");
    const Highlight h{"first",        "/body/p[1].0",
                      "/body/p[9].8", "Complete passage across pages",
                      "Chapter",      QDateTime::currentDateTimeUtc()};
    auto overlapping = h;
    overlapping.id = "second";
    store.setHighlights({h, overlapping});
    require(store.save(), "Cannot save overlapping passages");
    const auto path = store.folder() + "/highlights.json";
    const auto saved = bytes(path);
    HighlightStore reopened(root.path(), "file:///book.epub", fingerprint, "Book");
    require(reopened.load() && reopened.highlights().size() == 2 && reopened.highlights()[0].text == h.text &&
                reopened.highlights()[0].start == h.start && reopened.highlights()[1].id == "second",
            "Passages did not round-trip independently");
    // Older version-one files have no color field.
    auto legacy = QJsonDocument::fromJson(saved).object();
    auto entries = legacy.value("highlights").toArray();
    for (int i = 0; i < entries.size(); ++i) {
        auto entry = entries[i].toObject();
        entry.remove("color");
        entry.remove("style");
        entries[i] = entry;
    }
    legacy["highlights"] = entries;
    replace(path, QJsonDocument(legacy).toJson());
    require(reopened.load() && reopened.highlights().first().color == "gray", "Legacy passage is not gray");
    require(reopened.highlights().first().style == "shaded", "Legacy passage is not shaded");
    for (const auto &color : {"gray", "yellow", "green", "blue", "pink", "orange"}) {
        auto colored = h;
        colored.color = QString::fromLatin1(color);
        store.setHighlights({colored});
        require(store.save() && reopened.load() && reopened.highlights().first().color == colored.color,
                "Stored color did not round-trip");
        require(highlightDisplayColor(colored.color) == (colored.color == "orange" ? "gray" : colored.color),
                "Unknown future color did not display gray");
    }
    for (const auto &style : {"shaded", "underline", "dotted", "future"}) {
        auto styled = h;
        styled.style = style;
        store.setHighlights({styled});
        require(store.save() && reopened.load() && reopened.highlights().first().style == style,
                "Stored style did not round-trip");
        require(highlightDisplayStyle(style) == (QString(style) == "future" ? "shaded" : QString(style)),
                "Future style did not fall back to shaded");
    }
    auto invalidStyle = h;
    invalidStyle.style = "INVALID!";
    store.setHighlights({invalidStyle});
    const auto beforeInvalidStyle = bytes(path);
    require(!store.save() && bytes(path) == beforeInvalidStyle, "Invalid style replaced saved data");
    auto invalidColor = h;
    invalidColor.color = "INVALID!";
    store.setHighlights({invalidColor});
    const auto beforeInvalidColor = bytes(path);
    require(!store.save() && bytes(path) == beforeInvalidColor, "Invalid color replaced saved data");
    auto invalidObject = QJsonDocument::fromJson(beforeInvalidColor).object();
    auto invalidEntries = invalidObject.value("highlights").toArray();
    auto invalidEntry = invalidEntries.first().toObject();
    invalidEntry["color"] = 123;
    invalidEntries[0] = invalidEntry;
    invalidObject["highlights"] = invalidEntries;
    const auto invalidBytes = QJsonDocument(invalidObject).toJson();
    replace(path, invalidBytes);
    HighlightStore invalidStored(root.path(), "file:///book.epub", fingerprint, "Book");
    require(!invalidStored.load() && !invalidStored.save() && bytes(path) == invalidBytes,
            "Malformed color was overwritten");
    replace(path, saved);
    auto automatic = h;
    automatic.chosenEnd = "/body/p[9].7";
    automatic.sentenceEndPolicy = true;
    automatic.chosenStart = "/body/p[1].1";
    automatic.sentenceStartPolicy = true;
    store.setHighlights({automatic});
    require(store.save() && reopened.load() && reopened.highlights().first().sentenceEndPolicy &&
                reopened.highlights().first().chosenEnd == automatic.chosenEnd &&
                reopened.highlights().first().sentenceStartPolicy &&
                reopened.highlights().first().chosenStart == automatic.chosenStart,
            "Automatic endpoint provenance did not survive save and reopen");
    require(reopened.highlights().first().end == h.end, "Provenance changed effective saved endpoint");
    store.setHighlights({h, overlapping});
    require(store.save() && reopened.load() && !reopened.highlights().first().sentenceEndPolicy &&
                reopened.highlights().first().chosenEnd.isEmpty() &&
                !reopened.highlights().first().sentenceStartPolicy &&
                reopened.highlights().first().chosenStart.isEmpty(),
            "Legacy/manual exact endpoints acquired automatic policy");
    BookmarkStore bookmarks(root.path(), "file:///book.epub", fingerprint, "Book");
    require(bookmarks.load(), "Cannot load bookmarks beside highlights");
    bookmarks.setBookmarks({{h.start, "Chapter", "Excerpt", h.created}});
    require(bookmarks.save() && bytes(path) == saved, "Bookmark save changed highlights");
    HighlightStore other(root.path(), "different-server-book", {}, "Other");
    require(other.load() && other.highlights().isEmpty(), "Highlights leaked across books");
    HighlightStore edited(root.path(), "file:///book.epub", QByteArray(64, 'b'), "Book");
    require(edited.load() && edited.highlights().size() == 2 && edited.save(), "Edited sideload lost passages");
    auto bad = h;
    bad.text = QString(HighlightStore::maxTextUnits + 1, 'x');
    store.setHighlights({bad});
    require(!store.save() && bytes(path) == saved, "Oversized passage replaced saved data");
    store.setHighlights({h, h});
    require(!store.save() && bytes(path) == saved, "Duplicate IDs replaced saved data");
    for (const auto &damaged :
         {QByteArray("{"), QByteArray("{\"version\":2,\"highlights\":[]}"),
          QByteArray("{\"version\":1,\"highlights\":[{}]}"), QByteArray("{\"version\":1,\"highlights\":{}}")}) {
        replace(path, damaged);
        HighlightStore corrupt(root.path(), "file:///book.epub", fingerprint, "Book");
        require(!corrupt.load(), "Damaged highlights accepted");
        corrupt.setHighlights({h});
        require(!corrupt.save() && bytes(path) == damaged, "Damaged bytes overwritten after failed load");
    }
    replace(path, saved);
    // Books without highlight files must not parse the rest of the library's metadata.
    QTemporaryDir library;
    require(library.isValid(), "Cannot create adoption library");
    for (int i = 0; i < 300; ++i) {
        const auto folder =
            library.path() + "/file-" + QString::fromLatin1(QByteArray::number(i, 16).rightJustified(64, '0'));
        require(QDir().mkpath(folder), "Cannot create unhighlighted folder");
        QJsonObject metadata;
        metadata["identity"] = QString("file:///unrelated-%1.epub").arg(i);
        replace(folder + "/book.json", QJsonDocument(metadata).toJson());
    }
    const QString adoptionIdentity = "file:///changed.epub";
    HighlightStore unmarked(library.path(), adoptionIdentity, QByteArray(64, 'a'), "Book");
    require(unmarked.load() && unmarked.highlights().isEmpty() && unmarked.adoptionMetadataReads() == 0,
            "Opening an unhighlighted book parsed unrelated metadata");
    require(unmarked.load() && unmarked.adoptionMetadataReads() == 0,
            "Reopening an unhighlighted book parsed unrelated metadata");
    unmarked.setHighlights({h});
    require(unmarked.save(), "Cannot save older highlight revision");
    HighlightStore recent(library.path(), adoptionIdentity, QByteArray(64, 'b'), "Book");
    require(recent.load() && recent.highlights().size() == 1 && recent.adoptionMetadataReads() == 1,
            "Changed book did not adopt the older highlight revision");
    auto newer = h;
    newer.id = "newer";
    recent.setHighlights({newer});
    require(recent.save(), "Cannot save newer highlight revision");
    const struct utimbuf oldTime {
        1000000000, 1000000000
    }, newTime{1000000100, 1000000100};
    require(utime(QFile::encodeName(unmarked.folder() + "/highlights.json").constData(), &oldTime) == 0 &&
                utime(QFile::encodeName(recent.folder() + "/highlights.json").constData(), &newTime) == 0,
            "Cannot order highlight revisions");
    HighlightStore adopted(library.path(), adoptionIdentity, QByteArray(64, 'c'), "Book");
    require(adopted.load() && adopted.highlights().size() == 1 && adopted.highlights().first().id == "newer" &&
                adopted.adoptionMetadataReads() == 2,
            "Changed book did not adopt its newest matching highlight revision");
    const QByteArray corruptHighlight("{broken");
    replace(recent.folder() + "/highlights.json", corruptHighlight);
    require(!adopted.load() && !adopted.save() && adopted.adoptionMetadataReads() == 2 &&
                bytes(recent.folder() + "/highlights.json") == corruptHighlight,
            "A corrupt newest highlight revision was ignored or overwritten");
    // An authoritative empty list must not adopt older highlights.
    store.setHighlights({});
    require(store.save(), "Cannot delete all highlights");
    require(reopened.load() && reopened.highlights().isEmpty(), "Deleted highlights returned after reopening");
    return 0;
}
