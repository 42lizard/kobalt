#pragma once
#include <QMargins>
#include <QtGlobal>

namespace kobalt::ui {
// Kobalt's UI is designed in pixels of a 300 ppi screen, the density of the
// Clara and Libra. Every size scales to the device's density, so a control has
// the same physical size on every screen.
inline constexpr int designPpi = 300;
namespace detail {
inline int ppi = designPpi;
}
// Set before any view is built. Views read sizes when they lay out, so a view
// built earlier keeps the sizes of the old density.
inline void setDensity(int ppi) {
    detail::ppi = ppi > 0 ? ppi : designPpi;
}
inline int density() {
    return detail::ppi;
}
// A design size in device pixels. A size that is not zero stays at least one
// pixel, so a rule never disappears.
inline int px(int design) {
    if (!design || detail::ppi == designPpi)
        return design;
    const int scaled = qRound(design * qreal(detail::ppi) / designPpi);
    return scaled ? scaled : (design > 0 ? 1 : -1);
}

// A metric in design pixels. It converts to device pixels where it is used. A
// sum of metrics scales each part and then adds them, so edges that add up on
// a 300 ppi screen still meet after rounding.
class Px {
public:
    constexpr explicit Px(int design) : parts{design, 0, 0, 0} {}
    operator int() const {
        return px(parts[0]) + px(parts[1]) + px(parts[2]) + px(parts[3]);
    }
    // The value on a 300 ppi screen.
    constexpr int design() const {
        return parts[0] + parts[1] + parts[2] + parts[3];
    }
    friend constexpr Px operator+(Px a, Px b) {
        Px sum = a;
        int next = 0;
        while (next < 4 && sum.parts[next])
            ++next;
        for (int part : b.parts)
            if (part)
                sum.parts[next++] = part;
        return sum;
    }

private:
    int parts[4];
};

class PxMargins {
public:
    constexpr PxMargins(Px left, Px top, Px right, Px bottom) : l(left), t(top), r(right), b(bottom) {}
    constexpr PxMargins(int left, int top, int right, int bottom) : l(left), t(top), r(right), b(bottom) {}
    constexpr Px left() const {
        return l;
    }
    constexpr Px top() const {
        return t;
    }
    constexpr Px right() const {
        return r;
    }
    constexpr Px bottom() const {
        return b;
    }
    operator QMargins() const {
        return {l, t, r, b};
    }

private:
    Px l, t, r, b;
};
}
