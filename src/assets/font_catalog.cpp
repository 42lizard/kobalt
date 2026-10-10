#include "font_catalog.h"
#include <QDir>
#include <QMap>
#include <algorithm>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

namespace kobalt {
bool FontSources::operator==(const FontSources &other) const {
    return bundled == other.bundled && systemDirectory == other.systemDirectory &&
           sideloadDirectory == other.sideloadDirectory && removableRoot == other.removableRoot;
}
bool EngineFontInputs::operator==(const EngineFontInputs &other) const {
    return sources == other.sources && fallbackFiles == other.fallbackFiles &&
           hyphenationDirectory == other.hyphenationDirectory;
}
QString fontFamilyKey(const QString &family) {
    auto key = family.normalized(QString::NormalizationForm_C).trimmed().toCaseFolded();
    // Earlier Kobalt builds shipped this family under its old collection name.
    if (key == "kf libron")
        key = "libron";
    return key;
}
FontWeightAxis fontWeightAxis(const QVector<ReadingFont> &fonts, const QString &family) {
    for (const auto &font : fonts)
        if (fontFamilyKey(font.family) == fontFamilyKey(family))
            return font.weightAxis;
    return {};
}
double normalizedOpticalSize(const FontOpticalSizeAxis &axis, double points) {
    if (!std::isfinite(points) || points <= 0)
        points = axis.defaultValue;
    if (!axis.available())
        return points > 0 ? std::round(points) : 0;
    return qBound(std::ceil(axis.minimum), std::round(points), std::floor(axis.maximum));
}
FontOpticalSizeAxis fontOpticalSizeAxis(const QVector<ReadingFont> &fonts, const QString &family) {
    for (const auto &font : fonts)
        if (fontFamilyKey(font.family) == fontFamilyKey(family))
            return font.opticalSizeAxis;
    return {};
}
QVector<ReadingFont> discoverReadingFonts(const FontSources &sources) {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library))
        return {};
    QVector<ReadingFont> result;
    QMap<QString, int> seen;
    const auto add = [&](const QString &path, FontOrigin origin) {
        FT_Face face = nullptr;
        if (FT_New_Face(library, path.toUtf8().constData(), 0, &face))
            return;
        const auto family = QString::fromUtf8(face->family_name);
        FontWeightAxis axis;
        FontOpticalSizeAxis optical;
        FT_MM_Var *variations = nullptr;
        if (!FT_Get_MM_Var(face, &variations) && variations) {
            for (FT_UInt i = 0; i < variations->num_axis; ++i) {
                const auto &value = variations->axis[i];
                if (value.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
                    axis = {int(value.minimum / 65536), int(value.maximum / 65536), int(value.def / 65536)};
                if (value.tag == FT_MAKE_TAG('o', 'p', 's', 'z'))
                    optical = {value.minimum / 65536.0, value.maximum / 65536.0, value.def / 65536.0};
            }
            FT_Done_MM_Var(library, variations);
        }
        FT_Done_Face(face);
        if (family.isEmpty())
            return;
        const auto key = fontFamilyKey(family);
        const bool copy = !sources.removableRoot.isEmpty() && path.startsWith(sources.removableRoot + '/');
        auto found = seen.constFind(key);
        if (found != seen.cend()) {
            auto &entry = result[*found];
            if (entry.origin == origin && !entry.files.contains(path)) {
                entry.files.append(path);
                entry.copyForQt |= copy;
                if (entry.opticalSizeAxis.available() && optical.available()) {
                    auto &range = entry.opticalSizeAxis;
                    range.minimum = qMax(range.minimum, optical.minimum);
                    range.maximum = qMin(range.maximum, optical.maximum);
                    if (range.available())
                        range.defaultValue = qBound(range.minimum, range.defaultValue, range.maximum);
                    else
                        range = {};
                } else
                    entry.opticalSizeAxis = {};
                if (entry.weightAxis.available() && axis.available()) {
                    entry.weightAxis.minimum = qMax(entry.weightAxis.minimum, axis.minimum);
                    entry.weightAxis.maximum = qMin(entry.weightAxis.maximum, axis.maximum);
                    if (entry.weightAxis.available())
                        entry.weightAxis.defaultValue =
                            qBound(entry.weightAxis.minimum, entry.weightAxis.defaultValue, entry.weightAxis.maximum);
                    else
                        entry.weightAxis = {};
                } else {
                    entry.weightAxis = {};
                }
            }
            return;
        }
        seen.insert(key, result.size());
        result.append({family, {path}, origin, copy, axis, optical});
    };
    const auto core = [](const QString &path) {
        return path.contains("/ebook-core/") || path.contains("/ebook-core-");
    };
    for (const auto &path : sources.bundled)
        if (core(path))
            add(path, FontOrigin::Recommended);
    for (const auto &source : {qMakePair(sources.systemDirectory, FontOrigin::BuiltIn),
                               qMakePair(sources.sideloadDirectory, FontOrigin::Sideloaded)}) {
        if (source.first.isEmpty())
            continue;
        const QDir directory(source.first);
        for (const auto &name : directory.entryList({"*.ttf", "*.otf"}, QDir::Files, QDir::Name))
            add(directory.filePath(name), source.second);
    }
    // The UI family remains available for renderer-generated image notices,
    // but it is not a recommended reading font from the Core Collection.
    for (const auto &path : sources.bundled)
        if (!core(path))
            add(path, FontOrigin::Interface);
    FT_Done_FreeType(library);
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.origin == b.origin ? a.family.compare(b.family, Qt::CaseInsensitive) < 0 : a.origin < b.origin;
    });
    return result;
}
}
