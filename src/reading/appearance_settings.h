#pragma once
#include "reading/reading_density.h"
#include <QJsonObject>
#include <array>

namespace kobalt {
inline constexpr auto defaultFontFace = "Libron";
inline constexpr int defaultLineSpacing = 122;
inline constexpr int defaultVariableFontWeight = 450;

namespace appearance {
inline constexpr std::array<const char *, 7> layoutKeys{
    "show_header", "show_footer", "header_text_size", "footer_text_size", "show_clock", "orientation", "auto_rotate"};
// Book layout takes precedence over legacy global layout values.
QJsonObject readerPreferences(const QJsonObject &global, const QJsonObject &book);
QJsonObject globalPreferences(QJsonObject preferences);
// The saved settings that decide how the book is laid out and drawn. A
// retained layout is reused only when all of them match.
inline constexpr std::array<const char *, 15> keys{
    "font_pixels",       "line_spacing",         "font_face",      "default_font_face", "margin_pixels",
    "top_margin_pixels", "bottom_margin_pixels", "text_alignment", "bionic_reading",    "weight_steps",
    "font_weight",       "font_optical_size",    "hyphenation",    "dark_mode",         "smooth_images"};
// Copies every appearance key from `from` into `to`. A key that `from` does
// not have is removed from `to`, so the engine uses its own default for it.
void copy(QJsonObject &to, const QJsonObject &from);
// True when both objects have the same value, or no value, for every key.
bool same(const QJsonObject &a, const QJsonObject &b);
// The typography that Restore appearance defaults selects. Dark mode and
// smooth images are display choices and are not part of it.
QJsonObject typographyDefaults();
// Empty for every identity except the benchmark-owned EPUB.
QJsonObject benchmarkTypography(const QString &identity);
// A global default changes new books and publisher fallback, not a saved selection.
void applyFontDefault(QJsonObject &saved, const QJsonObject &preferences);
}
}
