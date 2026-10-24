#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

namespace kobalt {
struct Highlight {
    QString id, start, end, text, chapter;
    QDateTime created;
    // Absent provenance in older files preserves their exact endpoint.
    QString chosenEnd = {};
    bool sentenceEndPolicy = false;
    QString chosenStart = {};
    bool sentenceStartPolicy = false;
    QString color = QStringLiteral("gray");
    QString style = QStringLiteral("shaded");
    QString note = {};
};
// Unknown future stored keys display gray without losing their original value.
bool isHighlightColor(const QString &color);
QString highlightDisplayColor(const QString &color);
bool isHighlightStyle(const QString &style);
QString highlightDisplayStyle(const QString &style);
// Separate passages keep their IDs even when their ranges overlap or reflow.
class HighlightStore {
public:
    static constexpr int maxHighlights = 2000;
    static constexpr int maxTextUnits = 256 * 1024;
    static constexpr int maxNoteUnits = 64 * 1024;
    static constexpr qint64 maxFileBytes = 8 * 1024 * 1024;
    HighlightStore(QString root, QString identity, QByteArray fingerprint, QString title);
    bool available() const {
        return !key_.isEmpty();
    }
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    QString folder() const;
    // Metadata files read while looking for a changed sideloaded book's highlights.
    int adoptionMetadataReads() const {
        return adoptionMetadataReads_;
    }
    const QVector<Highlight> &highlights() const {
        return highlights_;
    }
    void setHighlights(QVector<Highlight> highlights) {
        highlights_ = std::move(highlights);
    }

private:
    bool readFolder(const QString &folder, QString *error);
    QString root_, identity_, title_, key_;
    QByteArray fingerprint_;
    QVector<Highlight> highlights_;
    bool loadFailed_ = false;
    int adoptionMetadataReads_ = 0;
};
}
