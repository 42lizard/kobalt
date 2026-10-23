#pragma once
#include <QColor>
#include <QPainter>
#include <QRegion>
#include <QString>

namespace kobalt::ui {
inline QColor highlightTint(const QString &key) {
    if (key == "yellow") {
        return QColor("#e8df9b");
    }
    if (key == "green") {
        return QColor("#b8d2aa");
    }
    if (key == "blue") {
        return QColor("#aecbdc");
    }
    if (key == "pink") {
        return QColor("#dfb5c2");
    }
    return QColor("#d7d7d7");
}
// The page and its thumbnails use the same tint without obscuring text.
inline void paintHighlightTint(QPainter &painter, const QRegion &region, QColor color, bool dark) {
    if (region.isEmpty())
        return;
    painter.save();
    painter.setClipRegion(region);
    painter.setCompositionMode(dark ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply);
    painter.fillRect(region.boundingRect(), dark ? (color == highlightTint("gray")
                                                        ? QColor(64, 64, 64)
                                                        : QColor(qRound(color.red() * 0.3), qRound(color.green() * 0.3),
                                                                 qRound(color.blue() * 0.3)))
                                                 : color);
    painter.restore();
}
}
