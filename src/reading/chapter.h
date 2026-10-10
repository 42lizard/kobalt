#pragma once
#include <QString>
#include <QSet>
#include <QVector>

namespace kobalt {
// The platform supplies source locations; the engine resolves them without
// changing the reading position. Duplicate destinations remain separate rows.
struct ChapterTarget {
    QString title, location;
    int depth = 0, words = 0;
};
struct ChapterLink {
    QString title, position;
    int page = -1, depth = 0, words = 0;
};
struct ChapterSelection {
    int current = -1, next = -1, page = 0, pages = 0;
};

// Nickel's Qt6 counter uses the next merged entry as an interval boundary.
// A file row before a named anchor counts only the prefix, not the whole file.
// Before another file-start row it instead counts the whole file; skip that
// overlapping count and use the following intervals. Require coverage of every
// spine resource, including its prefix, before exposing a book total.
inline qint64 spineWordCount(const QVector<ChapterTarget> &spine, const QVector<ChapterTarget> &graph) {
    QSet<QString> resources;
    for (const auto &entry : spine) {
        if (entry.location.isEmpty() || entry.location.contains('#'))
            return -1;
        resources.insert(entry.location);
    }
    if (resources.isEmpty())
        return -1;
    QSet<QString> counted;
    QString current;
    qint64 total = 0;
    for (int i = 0; i < graph.size(); ++i) {
        const auto &entry = graph[i];
        const auto resource = entry.location.section('#', 0, 0);
        const auto fragment = entry.location.section('#', 1);
        if (entry.words < 0 || !resources.contains(resource))
            return -1;
        if (resource != current) {
            if (!fragment.isEmpty() || counted.contains(resource))
                return -1;
            counted.insert(resource);
            current = resource;
        }
        if (i + 1 < graph.size() && graph[i + 1].location.section('#', 0, 0) == resource &&
            graph[i + 1].location.section('#', 1).isEmpty()) {
            if (!fragment.isEmpty())
                return -1;
            continue;
        }
        total += entry.words;
    }
    return counted == resources ? total : -1;
}

// Chapter lengths in pages, for books where Nickel has no word counts, such as
// plain EPUB files. Each chapter runs to the next one that starts later; one
// that shares a page with the next still counts one page. A chapter without a
// page keeps its place with no length, so selections still index the list.
inline QVector<ChapterLink> chapterPageLengths(QVector<ChapterLink> contents, int pages) {
    bool any = false;
    for (int i = 0; i < contents.size(); ++i) {
        auto &chapter = contents[i];
        chapter.words = 0;
        if (chapter.page < 0 || chapter.page >= pages)
            continue;
        int end = pages;
        for (int j = i + 1; j < contents.size(); ++j)
            if (contents[j].page > chapter.page) {
                end = contents[j].page;
                break;
            }
        chapter.words = qMax(1, end - chapter.page);
        any = true;
    }
    return any ? contents : QVector<ChapterLink>();
}
}
