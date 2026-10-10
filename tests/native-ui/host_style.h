#pragma once
#include <QApplication>

namespace kobalt::test {
inline constexpr auto hostStyleProfile = "nickel-slider-80px";
inline void applyHostStyle() {
    QApplication::setStyle("Fusion");
    // Nickel's application stylesheet gives QSlider an 80-pixel minimum and
    // maximum height. Kobalt's custom slider must override that allocation.
    // Stock Qt otherwise gives it a smaller hint, moving the controls below
    // it. Reproduce this measured host condition without copying Nickel's
    // stylesheet or changing production widget geometry.
    qApp->setStyleSheet("QSlider { min-height: 80px; max-height: 80px; }");
}
}
