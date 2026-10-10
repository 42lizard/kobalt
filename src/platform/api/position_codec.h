#pragma once
#include "reading/chapter.h"
#include <QString>

namespace kobalt {
class ReaderModel;
// Optional host position mapping. Calls run on the engine worker. No GUI
// object, native handle or document pointer crosses this value boundary.
struct PositionCodec {
    QString (*decode)(const ReaderModel &, const QString &, QString *) = nullptr;
    QString (*encode)(const ReaderModel &, QString *) = nullptr;
    QString (*chapterStart)(const QString &) = nullptr;
    explicit operator bool() const {
        return decode && encode && chapterStart;
    }
};
struct ResolvedChapters {
    QVector<ChapterLink> contents, graph;
};
ResolvedChapters resolveChapterTargets(const ReaderModel &model, const PositionCodec &codec,
                                       const QVector<ChapterTarget> &contents, const QVector<ChapterTarget> &graph);
}
