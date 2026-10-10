#pragma once
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "variable_fonts.h"

namespace kobalt::test {
inline AssetBundle &readerAssets() {
#ifdef KOBALT_TEST_ASSET_ROOT
    static AssetBundle assets(QStringLiteral(KOBALT_TEST_ASSET_ROOT), QStringLiteral(KOBALT_VERSION));
#else
    // ARM model checks stage the release bundle at the device fixture path.
    static AssetBundle assets(QStringLiteral("/mnt/onboard/.adds/kobalt/assets"), QStringLiteral(KOBALT_VERSION));
#endif
    return assets;
}
inline FontCopies &readerFontCopies() {
    static FontCopies copies;
    return copies;
}
inline EngineFontInputs readerFonts() {
#ifdef KOBALT_SYSTEM_FONT_DIRECTORY
    const QString system = QStringLiteral(KOBALT_SYSTEM_FONT_DIRECTORY);
#else
    const QString system;
#endif
    return variableFontInputs(
        engineFontInputs(readerAssets(), system, QStringLiteral("/mnt/onboard/fonts"), QStringLiteral("/mnt/onboard")));
}
inline QString readerPlaceholder() {
    static const QString family = placeholderFontFamily(readerAssets(), readerFontCopies());
    return family;
}
}
