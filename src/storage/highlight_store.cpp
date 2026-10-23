#include "highlight_store.h"
#include "bookmark_store.h"
#include "reader_storage.h"
#include "support/qt_compat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace kobalt {
namespace {
bool problem(QString *error, const char *text) {
    if (error) {
        *error = QString::fromLatin1(text);
    }
    return false;
}
bool write(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool colorToken(const QString &color) {
    if (color.isEmpty() || color.size() > 64) {
        return false;
    }
    for (const auto c : color) {
        if (c < QLatin1Char('a') || c > QLatin1Char('z')) {
            return false;
        }
    }
    return true;
}
bool valid(const Highlight &h) {
    return !h.id.isEmpty() && h.id.size() <= 128 && !h.start.isEmpty() && h.start.size() <= 4096 && !h.end.isEmpty() &&
           h.end.size() <= 4096 && (!h.sentenceEndPolicy || (!h.chosenEnd.isEmpty() && h.chosenEnd.size() <= 4096)) &&
           (!h.sentenceStartPolicy || (!h.chosenStart.isEmpty() && h.chosenStart.size() <= 4096)) && h.start != h.end &&
           !h.text.isEmpty() && h.text.size() <= HighlightStore::maxTextUnits && h.chapter.size() <= 512 &&
           h.created.isValid() && colorToken(h.color) && colorToken(h.style);
}
}
bool isHighlightColor(const QString &color) {
    return color == QLatin1String("gray") || color == QLatin1String("yellow") || color == QLatin1String("green") ||
           color == QLatin1String("blue") || color == QLatin1String("pink");
}
QString highlightDisplayColor(const QString &color) {
    return isHighlightColor(color) ? color : QStringLiteral("gray");
}
bool isHighlightStyle(const QString &style) {
    return style == QLatin1String("shaded") || style == QLatin1String("underline") || style == QLatin1String("dotted");
}
QString highlightDisplayStyle(const QString &style) {
    return isHighlightStyle(style) ? style : QStringLiteral("shaded");
}
HighlightStore::HighlightStore(QString root, QString identity, QByteArray fingerprint, QString title)
    : root_(std::move(root)), identity_(std::move(identity)), title_(std::move(title)),
      key_(BookmarkStore::bookKey(identity_, fingerprint)), fingerprint_(std::move(fingerprint)) {}
QString HighlightStore::folder() const {
    return key_.isEmpty() ? QString() : root_ + '/' + key_;
}
bool HighlightStore::readFolder(const QString &folder, QString *error) {
    const auto read = storage::readJsonFile(folder + "/highlights.json", maxFileBytes);
    if (read.status == storage::ReadStatus::Missing) {
        highlights_.clear();
        return true;
    }
    if (read.status != storage::ReadStatus::Ok) {
        return problem(error, "invalid");
    }
    const auto object = read.object;
    if (object.value("version").toInt(-1) != 1 || !object.value("highlights").isArray()) {
        return problem(error, "unsupported-format");
    }
    QVector<Highlight> loaded;
    QSet<QString> ids;
    const auto list = object.value("highlights").toArray();
    if (list.size() > maxHighlights) {
        return problem(error, "storage-limit");
    }
    for (const auto &value : list) {
        const auto o = value.toObject();
        Highlight h{
            o.value("id").toString(),      o.value("start").toString(),
            o.value("end").toString(),     o.value("text").toString(),
            o.value("chapter").toString(), QDateTime::fromString(o.value("created").toString(), Qt::ISODate).toUTC()};
        if (o.contains("color")) {
            if (!o.value("color").isString()) {
                return problem(error, "invalid-highlight");
            }
            h.color = o.value("color").toString();
        }
        if (o.contains("style")) {
            if (!o.value("style").isString())
                return problem(error, "invalid-highlight");
            h.style = o.value("style").toString();
        }
        if (o.contains("sentence_end")) {
            const auto provenance = o.value("sentence_end");
            const auto policy = provenance.toObject();
            if (!provenance.isObject() || policy.value("enabled") != QJsonValue(true) ||
                !policy.value("chosen_end").isString()) {
                return problem(error, "invalid-highlight");
            }
            h.chosenEnd = policy.value("chosen_end").toString();
            h.sentenceEndPolicy = true;
        }
        if (o.contains("sentence_start")) {
            const auto provenance = o.value("sentence_start");
            const auto policy = provenance.toObject();
            if (!provenance.isObject() || policy.value("enabled") != QJsonValue(true) ||
                !policy.value("chosen_start").isString()) {
                return problem(error, "invalid-highlight");
            }
            h.chosenStart = policy.value("chosen_start").toString();
            h.sentenceStartPolicy = true;
        }
        // Refuse edits to damaged lists rather than silently overwriting lost passages.
        if (!valid(h) || ids.contains(h.id)) {
            return problem(error, "invalid-highlight");
        }
        ids.insert(h.id);
        loaded.append(h);
    }
    highlights_ = std::move(loaded);
    return true;
}
bool HighlightStore::load(QString *error) {
    highlights_.clear();
    adoptionMetadataReads_ = 0;
    loadFailed_ = true;
    if (!available()) {
        return problem(error, "no-stable-key");
    }
    QString source = folder();
    if (!QFile::exists(source + "/highlights.json") && identity_.startsWith("file://")) {
        const auto folders = QDir(root_).entryList({"file-*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QDateTime newest;
        for (int i = 0; i < folders.size() && i < 10000; ++i) {
            const auto candidate = root_ + '/' + folders[i];
            const auto modified = QFileInfo(candidate + "/highlights.json").lastModified();
            if (!modified.isValid()) {
                continue;
            }
            ++adoptionMetadataReads_;
            const auto metadata = storage::readJsonFile(candidate + "/book.json", BookmarkStore::maxFileBytes);
            if (metadata.status == storage::ReadStatus::Ok &&
                metadata.object.value("identity").toString() == identity_ && (!newest.isValid() || modified > newest)) {
                newest = modified;
                source = candidate;
            }
        }
    }
    loadFailed_ = !readFolder(source, error);
    return !loadFailed_;
}
bool HighlightStore::save(QString *error) {
    if (loadFailed_) {
        return problem(error, "load-failed");
    }
    if (!available()) {
        return problem(error, "no-stable-key");
    }
    if (highlights_.size() > maxHighlights) {
        return problem(error, "storage-limit");
    }
    QJsonArray list;
    QSet<QString> ids;
    for (const auto &h : highlights_) {
        if (!valid(h) || ids.contains(h.id)) {
            return problem(error, "invalid-highlight");
        }
        ids.insert(h.id);
        QJsonObject item = qt::object({{"id", h.id},
                                       {"start", h.start},
                                       {"end", h.end},
                                       {"text", h.text},
                                       {"chapter", h.chapter},
                                       {"created", h.created.toUTC().toString(Qt::ISODate)}});
        item["color"] = h.color;
        item["style"] = h.style;
        if (h.sentenceEndPolicy) {
            item["sentence_end"] = qt::object({{"enabled", true}, {"chosen_end", h.chosenEnd}});
        }
        if (h.sentenceStartPolicy) {
            item["sentence_start"] = qt::object({{"enabled", true}, {"chosen_start", h.chosenStart}});
        }
        list.append(item);
    }
    const auto bytes = QJsonDocument(qt::object({{"version", 1}, {"highlights", list}})).toJson();
    const auto metadata = QJsonDocument(qt::object({{"version", 1},
                                                    {"identity", identity_},
                                                    {"title", title_},
                                                    {"fingerprint", QString::fromLatin1(fingerprint_)}}))
                              .toJson();
    if (bytes.size() > maxFileBytes || metadata.size() > BookmarkStore::maxFileBytes) {
        return problem(error, "storage-limit");
    }
    if (!QDir().mkpath(folder())) {
        return problem(error, "create-directory");
    }
    if (!write(folder() + "/book.json", metadata)) {
        return problem(error, "write-metadata");
    }
    if (!write(folder() + "/highlights.json", bytes)) {
        return problem(error, "write-highlights");
    }
    return true;
}
}
