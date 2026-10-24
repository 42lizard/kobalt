#include "reader_storage.h"
#include "annotation_database.h"
#include "bookmark_store.h"
#include "reading/appearance_settings.h"
#include "support/diagnostics.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QRegularExpression>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QCryptographicHash>

namespace kobalt::storage {
namespace {
QString child(const QString &directory, const QString &name) {
    return directory.isEmpty() ? QString() : directory + '/' + name;
}
}
QString Paths::preferences() const {
    return child(directory, "reader-settings.json");
}
QString Paths::readingSpeed() const {
    return child(directory, "reading-speed.json");
}
QString Paths::selection() const {
    return child(directory, "config.json");
}
QString Paths::annotations() const {
    return child(directory, "annotations");
}
QString Paths::database() const {
    return child(directory, "kobalt.sqlite");
}
QString Paths::layoutCache() const {
    return child(directory, "cache/reopen-layout.json");
}
QString Paths::documentCache() const {
    return child(directory, ".cache");
}
QString Paths::bookState(const QString &identity) const {
    return child(directory, bookPrefix +
                                QString::fromLatin1(
                                    QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex()) +
                                ".json");
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
JsonRead readJsonFile(const QString &path, qint64 maxBytes) {
    QFile file(path);
    if (!file.exists())
        return {{}, ReadStatus::Missing};
    if (!file.open(QIODevice::ReadOnly))
        return {{}, ReadStatus::Unreadable};
    maxBytes = qMax<qint64>(0, maxBytes);
    if (file.size() > maxBytes)
        return {{}, ReadStatus::TooLarge};
    // The file can grow after size() is checked. Read in chunks and stop one
    // byte past the limit, so a large limit never reserves a large buffer.
    QByteArray bytes;
    for (;;) {
        const qint64 left = maxBytes - bytes.size();
        const auto chunk = file.read(qMin<qint64>(left, 64 * 1024 - 1) + 1);
        if (chunk.isEmpty())
            break;
        bytes += chunk;
        if (bytes.size() > maxBytes)
            return {{}, ReadStatus::TooLarge};
    }
    if (file.error() != QFileDevice::NoError)
        return {{}, ReadStatus::Unreadable};
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return {{}, ReadStatus::Invalid};
    return {document.object(), ReadStatus::Ok};
}
const char *readStatusName(ReadStatus status) {
    switch (status) {
    case ReadStatus::Ok:
        return "ok";
    case ReadStatus::Missing:
        return "missing";
    case ReadStatus::Unreadable:
        return "unreadable";
    case ReadStatus::TooLarge:
        return "too-large";
    case ReadStatus::Invalid:
        return "invalid";
    case ReadStatus::Unsupported:
        return "unsupported";
    case ReadStatus::DatabaseError:
        return "database-error";
    }
    return "unknown";
}
QJsonObject readJson(const QString &path) {
    return readJsonFile(path).object;
}
namespace {
QByteArray fieldBytes(const QJsonValue &value) {
    switch (value.type()) {
    case QJsonValue::Bool: return value.toBool() ? "1" : "0";
    case QJsonValue::Double: return QByteArray::number(value.toDouble(), 'g', 17);
    case QJsonValue::String: return value.toString().toUtf8();
    case QJsonValue::Array: return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    case QJsonValue::Object: return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    default: return QByteArray("");
    }
}
QString fieldText(const QByteArray &bytes) {
    // Prefix a character so Qt 5.2 does not consume a literal initial FEFF.
    const auto prefixed = QByteArray(" ") + bytes;
    return QString::fromUtf8(prefixed.constData(), prefixed.size()).mid(1);
}
bool putFields(QSqlDatabase &db, const QString &name, const QJsonObject &object, const QJsonObject &previous = {}) {
    QSqlQuery query(db);
    if (!query.prepare("INSERT OR IGNORE INTO records(name,format) VALUES(?,1)")) return false;
    query.addBindValue(name);
    if (!query.exec()) return false;
    for (auto it = previous.begin(); it != previous.end(); ++it) {
        if (object.contains(it.key())) continue;
        if (!query.prepare("DELETE FROM record_fields WHERE record=? AND key=?")) return false;
        query.addBindValue(name); query.addBindValue(it.key());
        if (!query.exec()) return false;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (previous.contains(it.key()) && previous.value(it.key()) == it.value()) continue;
        if (!query.prepare("INSERT OR REPLACE INTO record_fields(record,key,type,value) VALUES(?,?,?,?)")) return false;
        query.addBindValue(name); query.addBindValue(it.key()); query.addBindValue(int(it.value().type()));
        auto bytes = fieldBytes(it.value());
        if (bytes.isNull()) bytes = QByteArray("");
        query.addBindValue(bytes);
        if (!query.exec()) return false;
    }
    return true;
}
// Archiving follows commit. If it is interrupted, the import ledger still
// prevents the source from overwriting newer database data on the next run.
void archiveSource(const QString &directory, const QString &source, const QByteArray &digest) {
    if (source.isEmpty() || digest.isEmpty()) return;
    const QFileInfo info(source);
    if (info.isSymLink() || info.absolutePath() != QDir(directory).absolutePath()) return;
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) return;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result() != digest) return;
    file.close();
    const auto backup = directory + "/migration-backup";
    if (!QDir().mkpath(backup)) return;
    const auto destination = backup + '/' + info.fileName();
    if (!QFileInfo::exists(destination)) QFile::rename(source, destination);
    else {
        QFile previous(destination);
        QCryptographicHash archived(QCryptographicHash::Sha256);
        if (previous.open(QIODevice::ReadOnly) && archived.addData(&previous) && archived.result() == digest)
            QFile::remove(source);
    }
}
bool recordTransaction(const QString &directory, const QString &name, const QString &legacy,
                       qint64 maxBytes, JsonRead *read, const QJsonObject *replacement) {
    if (directory.isEmpty() || name.isEmpty() || maxBytes < 0) {
        read->status = ReadStatus::Invalid;
        return false;
    }
    QByteArray importedDigest;
    const bool ok = AnnotationDatabase::transaction(directory, [&](QSqlDatabase &db) {
        QSqlQuery query(db);
        if (!query.prepare("SELECT format FROM records WHERE name=?")) return false;
        query.addBindValue(name);
        if (!query.exec()) return false;
        const bool exists = query.next();
        if (exists && query.value(0).toInt() != 1) { read->status = ReadStatus::Unsupported; return false; }
        query.finish();
        QJsonObject object;
        if (exists) {
            if (!query.prepare("SELECT coalesce(sum(length(value)+length(key)),0) FROM record_fields WHERE record=?")) return false;
            query.addBindValue(name);
            if (!query.exec() || !query.next() || query.value(0).toLongLong() > maxBytes) return false;
            query.finish();
            if (!query.prepare("SELECT key,type,value FROM record_fields WHERE record=?")) return false;
            query.addBindValue(name);
            if (!query.exec()) return false;
            while (query.next()) {
                const int type = query.value(1).toInt();
                const auto bytes = query.value(2).toByteArray();
                QJsonValue value;
                if (type == QJsonValue::Bool) {
                    if (bytes != "0" && bytes != "1") return false;
                    value = bytes == "1";
                } else if (type == QJsonValue::Double) {
                    bool valid = false; const double number = bytes.toDouble(&valid);
                    if (!valid) return false;
                    value = number;
                } else if (type == QJsonValue::String) value = fieldText(bytes);
                else if (type == QJsonValue::Array || type == QJsonValue::Object) {
                    QJsonParseError error;
                    const auto document = QJsonDocument::fromJson(bytes, &error);
                    if (error.error != QJsonParseError::NoError ||
                        (type == QJsonValue::Array ? !document.isArray() : !document.isObject())) return false;
                    value = type == QJsonValue::Array ? QJsonValue(document.array()) : QJsonValue(document.object());
                } else if (type != QJsonValue::Null || !bytes.isEmpty()) return false;
                object.insert(query.value(0).toString(), value);
            }
            if (query.lastError().isValid()) return false;
            query.finish();
            if (!query.prepare("SELECT digest FROM imports WHERE source=? AND record=?")) return false;
            query.addBindValue(legacy); query.addBindValue(name);
            if (!query.exec()) return false;
            if (query.next()) importedDigest = query.value(0).toByteArray();
        } else if (!legacy.isEmpty()) {
            *read = readJsonFile(legacy, maxBytes);
            if (read->status != ReadStatus::Missing && read->status != ReadStatus::Ok) return false;
            object = read->object;
            if (read->status == ReadStatus::Ok && name.startsWith("book:") &&
                object.value("book").toString() != name.mid(5)) {
                read->status = ReadStatus::Invalid; return false;
            }
            if (object.contains("schema") && object.value("schema").toDouble(-1) != 1) {
                read->status = ReadStatus::Unsupported; return false;
            }
            if (read->status == ReadStatus::Ok) {
                QFile source(legacy);
                if (!source.open(QIODevice::ReadOnly)) return false;
                const auto bytes = source.read(maxBytes + 1);
                if (bytes.size() > maxBytes || QJsonDocument::fromJson(bytes).object() != object) return false;
                importedDigest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
                if (!putFields(db, name, object)) return false;
                if (!query.prepare("INSERT INTO imports(source,digest,record) VALUES(?,?,?)")) return false;
                query.addBindValue(legacy); query.addBindValue(importedDigest); query.addBindValue(name);
                if (!query.exec()) return false;
            }
        }
        if (replacement && (!exists || *replacement != object) && !putFields(db, name, *replacement, object)) return false;
        read->object = replacement ? *replacement : object;
        read->status = exists || replacement || !importedDigest.isEmpty() ? ReadStatus::Ok : ReadStatus::Missing;
        return true;
    });
    if (ok) archiveSource(directory, legacy, importedDigest);
    else {
        read->object = {};
        if (read->status == ReadStatus::Ok || read->status == ReadStatus::Missing) read->status = ReadStatus::DatabaseError;
    }
    return ok;
}
}
JsonRead readRecord(const QString &directory, const QString &name, const QString &legacy, qint64 maxBytes) {
    JsonRead result;
    recordTransaction(directory, name, legacy, maxBytes, &result, nullptr);
    return result;
}
void archiveImportedSource(const QString &directory, const QString &source) {
    QByteArray digest;
    const bool ok = AnnotationDatabase::transaction(directory, [&](QSqlDatabase &db) {
        QSqlQuery query(db);
        if (!query.prepare("SELECT digest FROM imports WHERE source=?")) return false;
        query.addBindValue(source);
        if (!query.exec()) return false;
        if (query.next()) digest = query.value(0).toByteArray();
        return true;
    });
    if (ok) archiveSource(directory, source, digest);
}
bool writeRecord(const QString &directory, const QString &name, const QString &legacy,
                 const QJsonObject &value, qint64 maxBytes) {
    if (QJsonDocument(value).toJson().size() > maxBytes ||
        (value.contains("schema") && value.value("schema").toDouble(-1) != 1)) return false;
    JsonRead result;
    return recordTransaction(directory, name, legacy, maxBytes, &result, &value);
}
JsonRead readBookState(const Paths &paths, const QString &identity, const QString &legacy) {
    auto result = readRecord(paths.directory, "book:" + identity, legacy.isEmpty() ? paths.bookState(identity) : legacy);
    if (result.status == ReadStatus::Ok && result.object.value("book").toString() != identity)
        return {{}, ReadStatus::Invalid};
    return result;
}
bool writeBookState(const Paths &paths, const QString &identity, const QJsonObject &value, const QString &legacy) {
    return value.value("book").toString() == identity &&
           writeRecord(paths.directory, "book:" + identity, legacy.isEmpty() ? paths.bookState(identity) : legacy, value);
}
JsonRead readPreferences(const QString &path) {
    auto read = readJsonFile(path);
    if (read.status != ReadStatus::Ok) return read;
    if (read.object.contains("schema") && read.object.value("schema").toDouble(-1) != 1)
        return {{}, ReadStatus::Unsupported};
    auto clean = appearance::cleanPreferences(read.object);
    clean["schema"] = 1;
    if (clean != read.object && !writeJson(path, clean)) diagnostic("settings-cleanup-unsaved", {}, false);
    return {clean, ReadStatus::Ok};
}
bool writePreferences(const QString &path, const QJsonObject &object) {
    const auto old = readJsonFile(path);
    if (old.status != ReadStatus::Ok && old.status != ReadStatus::Missing) return false;
    if ((old.object.contains("schema") && old.object.value("schema").toDouble(-1) != 1) ||
        (object.contains("schema") && object.value("schema").toDouble(-1) != 1)) return false;
    auto clean = appearance::cleanPreferences(object);
    clean["schema"] = 1;
    return writeJson(path, clean);
}
bool prepareProfile(const Paths &paths) {
    if (paths.directory.isEmpty()) return false;
    const QDir directory(paths.directory);
    bool ok = true;
    const QRegularExpression bookName("^" + QRegularExpression::escape(paths.bookPrefix) + "[0-9a-f]{64}\\.json$");
    for (const auto &file : directory.entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
        if (!bookName.match(file.fileName()).hasMatch()) continue;
        const auto read = readJsonFile(file.absoluteFilePath());
        const auto identity = read.object.value("book").toString();
        if (read.status != ReadStatus::Ok || identity.isEmpty() ||
            QFileInfo(paths.bookState(identity)).fileName() != file.fileName()) { ok = false; continue; }
        if (readBookState(paths, identity).status != ReadStatus::Ok) ok = false;
    }
    for (const auto *name : {"reading-speed", "last-book"}) {
        const auto source = directory.filePath(QString::fromLatin1(name) + ".json");
        if (QFileInfo::exists(source) &&
            readRecord(paths.directory, QString::fromLatin1(name), source,
                       qstrcmp(name, "last-book") == 0 ? 256 * 1024 : maxJsonBytes).status != ReadStatus::Ok) ok = false;
    }
    if (annotationDatabaseEnabled()) {
        const QDir annotations(paths.annotations());
        const auto folders = annotations.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const auto &folder : folders) {
            if (folder.fileName() == ".index") continue;
            const auto metadata = readJsonFile(folder.filePath() + "/book.json", BookmarkStore::maxFileBytes);
            const auto identity = metadata.object.value("identity").toString();
            const auto fingerprint = metadata.object.value("fingerprint").toString().toLatin1();
            if (metadata.status != ReadStatus::Ok || metadata.object.value("version") != QJsonValue(1) ||
                identity.isEmpty() || BookmarkStore::bookKey(identity, fingerprint) != folder.fileName()) {
                ok = false; continue;
            }
            bool recognized = true;
            for (const auto &entry : QDir(folder.filePath()).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
                if (!entry.isFile() || entry.isSymLink() ||
                    (entry.fileName() != "book.json" && entry.fileName() != "bookmarks.json" && entry.fileName() != "highlights.json")) {
                    recognized = false; break;
                }
                const auto source = readJsonFile(entry.filePath(), 8 * 1024 * 1024);
                if (source.status != ReadStatus::Ok || source.object.value("version") != QJsonValue(1)) recognized = false;
            }
            if (!recognized) { ok = false; continue; }
            QVector<Bookmark> bookmarks;
            AnnotationDatabaseState state;
            QString error;
            if (!AnnotationDatabase::load({paths.annotations(), identity, metadata.object.value("title").toString(),
                                          folder.fileName(), fingerprint, paths.database()}, &bookmarks, &state, &error)) {
                ok = false; continue;
            }
            const auto backup = paths.directory + "/migration-backup/annotations";
            if (!QDir().mkpath(backup) || !QDir().rename(folder.filePath(), backup + '/' + folder.fileName())) ok = false;
        }
        // The path lookup index is derived from the old annotation folders.
        // Keep it with the import snapshots rather than mixing it into caches.
        const auto index = annotations.filePath(".index");
        if (QFileInfo::exists(index) && !QFileInfo(index).isSymLink()) {
            const auto backup = paths.directory + "/migration-backup/annotations";
            if (!QDir().mkpath(backup) || !QDir().rename(index, backup + "/.index")) ok = false;
        }
        QDir().rmdir(paths.annotations());
    }
    if (QFileInfo::exists(paths.preferences())) {
        const auto settings = readPreferences(paths.preferences());
        if (settings.status != ReadStatus::Ok) ok = false;
    }
    const auto oldCache = directory.filePath("reopen-layout.json");
    if (QFileInfo::exists(oldCache) && !QFileInfo(oldCache).isSymLink()) {
        const auto read = readJsonFile(oldCache);
        if (read.status == ReadStatus::Ok && read.object.value("version").toInt() == 1) {
            QDir().mkpath(QFileInfo(paths.layoutCache()).absolutePath());
            if (!QFileInfo::exists(paths.layoutCache())) QFile::rename(oldCache, paths.layoutCache());
            else QFile::remove(oldCache); // Both are disposable inspection results.
        }
    }
    // Startup can already have created the new diagnostic log. Keep the old
    // reports with the import snapshots so neither copy replaces the other.
    const auto diagnostics = directory.filePath("migration-backup/diagnostics");
    for (const auto *name : {"state.json", "session.json", "events.jsonl", "events.jsonl.previous"}) {
        const auto source = directory.filePath(QString::fromLatin1(name));
        if (QFileInfo::exists(source) && !QFileInfo(source).isSymLink()) {
            if (!QDir().mkpath(diagnostics) || !QFile::rename(source, diagnostics + '/' + QString::fromLatin1(name)))
                ok = false;
        }
    }
    return ok;
}
}
