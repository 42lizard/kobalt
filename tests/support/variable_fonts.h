#pragma once
#include "assets/font_catalog.h"
#include <QtGlobal>

namespace kobalt::test {
// External fixtures exercise real font axes without bundling a prototype.
inline EngineFontInputs variableFontInputs(EngineFontInputs inputs) {
    const auto directory = QString::fromLocal8Bit(qgetenv("KOBALT_TEST_VARIABLE_FONTS"));
    if (!directory.isEmpty())
        inputs.sources.sideloadDirectory = directory;
    return inputs;
}
inline ReadingFont variableFont(const EngineFontInputs &inputs) {
    for (const auto &font : discoverReadingFonts(inputs.sources))
        if (font.weightAxis.available())
            return font;
    return {};
}
}
