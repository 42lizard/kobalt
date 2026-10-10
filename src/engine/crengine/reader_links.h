#pragma once
#include <QString>
#include <QRect>
#include <QVector>

namespace kobalt {
struct PageLink {
    QString source;
    QVector<QRect> rectangles;
};
enum class LinkKind { None, Passage, Footnote, Website, Unavailable };
struct LinkDetails {
    LinkKind kind = LinkKind::None;
    QString label, target, html, error;
    // The chapter that contains the target, for passages and notes.
    QString chapter;
};
}
