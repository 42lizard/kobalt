#pragma once

#include <QImage>
#include <QRegion>

namespace kobalt {

inline bool hasVisibleColour(QRgb pixel) {
    const int red = qRed(pixel), green = qGreen(pixel), blue = qBlue(pixel);
    return qMax(red, qMax(green, blue)) - qMin(red, qMin(green, blue)) > 10;
}

// Return the page areas that need a full refresh after a page turn.
QRegion pageRefreshRegions(const QImage &image, bool colourDevice);

}
