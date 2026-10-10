#pragma once
#include "qt_compat.h"
#include <QFont>
#include <QFontMetrics>

namespace kobalt::qt {
#if QT_VERSION < QT_VERSION_CHECK(5, 5, 0)
constexpr QFont::Weight mediumWeight = QFont::Weight(57);
#else
constexpr QFont::Weight mediumWeight = QFont::Medium;
#endif
template <class Text> int textWidth(const QFontMetrics &metrics, const Text &text) {
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
    return metrics.width(text);
#else
    return metrics.horizontalAdvance(text);
#endif
}
}
