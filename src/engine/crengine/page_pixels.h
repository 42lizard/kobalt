#pragma once
#include "model/page_refresh_regions.h"
#include <QImage>
#include <QVector>
#if defined(__arm__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace kobalt {
// CREngine supplies 0x00RRGGBB pixels. Convert them to opaque Qt pixels and
// record every coloured tile, including faint antialiased edges.
inline bool normalizePagePixels(QImage &image, bool dark, QVector<uchar> *colourTiles, int tileColumns) {
    const int width = image.width(), height = image.height();
    quint32 colourBits = 0;
    const quint32 inversion = dark ? 0x00ffffffU : 0;
    // Keep the compiler-generated loop on ARM64.
#if defined(__arm__) && defined(__ARM_NEON)
    uint32x4_t vectorColourBits = vdupq_n_u32(0);
    const uint32x4_t vectorInversion = vdupq_n_u32(inversion);
    const uint32x4_t opaque = vdupq_n_u32(0xff000000U);
#endif
    for (int y = 0; y < height; ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        int x = 0;
#if defined(__arm__) && defined(__ARM_NEON)
        for (; x + 4 <= width; x += 4) {
            const uint32x4_t pixel = vld1q_u32(pixels + x);
            const uint32x4_t green = vshrq_n_u32(pixel, 8);
            vectorColourBits = vorrq_u32(vectorColourBits,
                                         vorrq_u32(veorq_u32(vshrq_n_u32(pixel, 16), green), veorq_u32(green, pixel)));
            vst1q_u32(pixels + x, vorrq_u32(veorq_u32(pixel, vectorInversion), opaque));
        }
#endif
        for (; x < width; ++x) {
            const quint32 pixel = pixels[x];
            colourBits |= ((pixel >> 16) ^ (pixel >> 8)) | ((pixel >> 8) ^ pixel);
            pixels[x] = (pixel ^ inversion) | 0xff000000U;
        }
    }
#if defined(__arm__) && defined(__ARM_NEON)
    colourBits |= vgetq_lane_u32(vectorColourBits, 0) | vgetq_lane_u32(vectorColourBits, 1) |
                  vgetq_lane_u32(vectorColourBits, 2) | vgetq_lane_u32(vectorColourBits, 3);
#endif
    // Grayscale pages need no colour regions. Keep their pixel conversion
    // free of tile writes and per-pixel colour thresholds.
    if (!(colourBits & 0xffU))
        return false;
    bool hasColour = false;
    auto *tiles = colourTiles ? colourTiles->data() : nullptr;
    for (int y = 0; y < height; ++y) {
        const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = pixels[x];
            if (qRed(pixel) == qGreen(pixel) && qGreen(pixel) == qBlue(pixel))
                continue;
            if (hasVisibleColour(pixel))
                hasColour = true;
            if (tiles)
                tiles[(y / 32) * tileColumns + x / 32] = 1;
        }
    }
    return hasColour;
}
}
