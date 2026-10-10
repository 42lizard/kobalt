#include "interfaces/reader/ui/style/font_data.h"
#include "interfaces/reader/ui/style/fonts.h"
#ifdef KOBALT_INSTALLED_FONTS
#include "reader_inputs.h"
#endif
#include "check.h"
#include <QFile>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <cmath>
#include <cstdio>

namespace {
// The source tree when the check runs beside it; on a simulated device, the
// fonts that Kobalt installed there.
QByteArray read(const char *name) {
    QFile file(QStringLiteral(KOBALT_FONT_DIRECTORY "/") + QLatin1String(name));
    if (file.open(QIODevice::ReadOnly))
        return file.readAll();
#ifdef KOBALT_INSTALLED_FONTS
    return kobalt::test::readerAssets().fontData(QLatin1String(name));
#else
    return {};
#endif
}
// A valid table directory around one simple glyph. The glyph bytes are the
// input under test; no font loader needs to accept this minimal fixture.
QByteArray fontWithGlyph(const QByteArray &glyph) {
    const auto set16 = [](QByteArray &bytes, int at, quint16 value) {
        bytes[at] = char(value >> 8);
        bytes[at + 1] = char(value);
    };
    const auto set32 = [&set16](QByteArray &bytes, int at, quint32 value) {
        set16(bytes, at, quint16(value >> 16));
        set16(bytes, at + 2, quint16(value));
    };
    QByteArray head(54, '\0'), maxp(6, '\0'), hhea(36, '\0'), hmtx(4, '\0'), loca(8, '\0');
    set16(head, 18, 2048);
    set16(head, 50, 1);
    set16(maxp, 4, 1);
    set16(hhea, 34, 1);
    set32(loca, 4, quint32(glyph.size()));
    const QVector<QPair<QByteArray, QByteArray>> tables{
        {"head", head},
        {"maxp", maxp},
        {"hhea", hhea},
        {"hmtx", hmtx},
        {"loca", loca},
        {"glyf", glyph},
        {"name", QByteArray(6, '\0')},
    };
    QByteArray result(12 + 16 * tables.size(), '\0');
    set32(result, 0, 0x00010000);
    set16(result, 4, quint16(tables.size()));
    for (int i = 0; i < tables.size(); ++i) {
        const int at = 12 + 16 * i;
        result.replace(at, 4, tables[i].first);
        set32(result, at + 8, quint32(result.size()));
        set32(result, at + 12, quint32(tables[i].second.size()));
        result.append(tables[i].second);
    }
    return result;
}
void malformedGlyphs() {
    // One contour ending at point 3, no instructions and four coincident
    // points. 0x31 means on-curve with zero x and y deltas.
    QByteArray glyph(14, '\0');
    glyph[1] = 1;
    glyph[11] = 3;
    const auto face = [](const QByteArray &bytes) {
        return kobalt::ui::interfaceFace(fontWithGlyph(bytes), "Kobalt Test", "Regular", 2);
    };
    require(!face(glyph + QByteArray(4, char(0x31))).isEmpty(), "Rejected complete glyph flags");
    require(face(glyph).isEmpty(), "Accepted missing glyph flags");
    require(face(glyph + QByteArray(1, char(0x31))).isEmpty(), "Accepted truncated glyph flags");
    require(face(glyph + QByteArray(1, char(0x39))).isEmpty(), "Accepted missing flag repeat count");
}
// Compare stored design advances for every long metric. Side bearings may
// move with the emboldened outline, but its horizontal advance must not.
void sameAdvanceWidths(const QRawFont &before, const QRawFont &after) {
    const auto beforeHeader = before.fontTable("hhea"), afterHeader = after.fontTable("hhea");
    require(beforeHeader.size() >= 36 && afterHeader.size() >= 36, "Missing horizontal metric headers");
    const auto count = [](const QByteArray &header) {
        return (int(uchar(header[34])) << 8) | uchar(header[35]);
    };
    const int metrics = count(beforeHeader);
    require(metrics > 0 && count(afterHeader) == metrics, "Changed horizontal metric count");
    const auto beforeMetrics = before.fontTable("hmtx"), afterMetrics = after.fontTable("hmtx");
    require(beforeMetrics.size() >= 4 * metrics && afterMetrics.size() >= 4 * metrics, "Truncated horizontal metrics");
    for (int glyph = 0; glyph < metrics; ++glyph)
        require(beforeMetrics.mid(4 * glyph, 2) == afterMetrics.mid(4 * glyph, 2), "Changed a stored advance width");
}
const QString sample = QStringLiteral("Hamburgefonstiv 0123 Settings");
// Summed coverage of the sample: the amount of ink, not the count of pixels.
double ink(const QRawFont &font) {
    QImage image(1400, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    const auto glyphs = font.glyphIndexesForString(sample);
    auto advances = font.advancesForGlyphIndexes(glyphs);
    QVector<QPointF> positions;
    qreal x = 10;
    for (const auto &advance : advances) {
        positions.append({x, 150});
        x += advance.x();
    }
    QGlyphRun run;
    run.setRawFont(font);
    run.setGlyphIndexes(glyphs);
    run.setPositions(positions);
    {
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.drawGlyphRun({0, 0}, run);
    }
    double sum = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            sum += 255 - qGray(image.pixel(x, y));
    return sum / 255;
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    malformedGlyphs();
    const struct {
        const char *file, *style;
    } faces[] = {
        {"IBMPlexSans-Regular.ttf", "Regular"},   {"IBMPlexSans-Bold.ttf", "Bold"},
        {"IBMPlexSans-Italic.ttf", "Italic"},     {"IBMPlexMono-Regular.ttf", "Regular"},
        {"IBMPlexMono-Bold.ttf", "Bold"},         {"InstrumentSerif-Regular.ttf", "Regular"},
        {"InstrumentSerif-Italic.ttf", "Italic"},
    };
    for (const auto &face : faces) {
        const auto source = read(face.file);
        require(!source.isEmpty(), "Bundled font is missing");
        // Test design geometry without grid fitting. Qt5's default hinter can
        // change an advance by one pixel when the outline grows, even when
        // the stored advance is unchanged. The 32px ink checks keep hinting.
        const QRawFont original(source, 256, QFont::PreferNoHinting);
        const auto glyphs = original.glyphIndexesForString(sample);

        // Without extra weight, only the names change.
        const auto renamed = kobalt::ui::interfaceFace(source, "Kobalt Test", face.style, 0);
        const QRawFont same(renamed, 256, QFont::PreferNoHinting);
        require(same.isValid() && same.familyName() == "Kobalt Test", "Renamed font has the wrong family");
        require(same.glyphIndexesForString(sample) == glyphs, "Renaming changed the glyph map");
        sameAdvanceWidths(original, same);
        require(same.advancesForGlyphIndexes(glyphs) == original.advancesForGlyphIndexes(glyphs),
                "Renaming changed advances");
        for (const auto glyph : glyphs)
            require(same.pathForGlyph(glyph) == original.pathForGlyph(glyph), "Renaming changed an outline");
        const int id = QFontDatabase::addApplicationFontFromData(renamed);
        require(QFontDatabase::applicationFontFamilies(id) == QStringList{"Kobalt Test"},
                "A record still names the original family");
        QFontDatabase::removeApplicationFont(id);

        // Two steps add 2/256 em to every stem, centred, and keep every advance.
        const auto heavier = kobalt::ui::interfaceFace(source, "Kobalt Test", face.style, 2);
        const QRawFont bold(heavier, 256, QFont::PreferNoHinting);
        require(bold.isValid() && bold.familyName() == "Kobalt Test", "Emboldened font has the wrong family");
        require(bold.glyphIndexesForString(sample) == glyphs, "Emboldening changed the glyph map");
        sameAdvanceWidths(original, bold);
        require(bold.advancesForGlyphIndexes(glyphs) == original.advancesForGlyphIndexes(glyphs),
                "Emboldening changed advances");
        // A slanted stem grows by more than its width at the corners.
        if (QLatin1String(face.style) != QLatin1String("Italic"))
            for (const auto character : {QChar('H'), QChar('l'), QChar('o'), QChar('0')}) {
                const auto glyph = original.glyphIndexesForString(QString(character)).value(0);
                const auto before = original.pathForGlyph(glyph).boundingRect(),
                           after = bold.pathForGlyph(glyph).boundingRect();
                // 2/256 of a 256 pixel em is 2 pixels. Font units round to about 0.13 pixels.
                require(std::abs(after.width() - before.width() - 2) < .35 &&
                            std::abs(after.height() - before.height() - 2) < .35,
                        "A stem did not grow by two steps");
                require(std::abs(after.left() - before.left() + 1) < .35 &&
                            std::abs(after.bottom() - before.bottom() - 1) < .35,
                        "Emboldening was not centred on the glyph");
            }
        // The interface's weight: clearly heavier, still well short of a bolder style.
        const auto chosen = kobalt::ui::interfaceFace(source, "Kobalt Test", face.style, kobalt::ui::heavierTextSteps);
        const double regularInk = ink(QRawFont(source, 32)), heavierInk = ink(QRawFont(chosen, 32));
        const double heaviestInk = ink(QRawFont(kobalt::ui::interfaceFace(source, "Kobalt Test", face.style, 8), 32));
        std::printf("%s: ink %.0f, interface weight %+.1f%%, eight steps %+.1f%%\n", face.file, regularInk,
                    100 * (heavierInk / regularInk - 1), 100 * (heaviestInk / regularInk - 1));
        require(heavierInk > regularInk * 1.04 && heavierInk < regularInk * 1.25,
                "The interface weight is not a moderate step");
        require(heaviestInk > heavierInk && heavierInk > ink(QRawFont(heavier, 32)),
                "More steps did not add more weight");
        // IBM Plex Sans Medium was judged too heavy for the interface.
        // Medium is not installed on a device, so only the source tree compares.
        const auto medium = read("IBMPlexSans-Medium.ttf");
        if (QLatin1String(face.file) == QLatin1String("IBMPlexSans-Regular.ttf") && !medium.isEmpty()) {
            const double mediumInk = ink(QRawFont(medium, 32));
            std::printf("IBMPlexSans-Medium.ttf: %+.1f%%\n", 100 * (mediumInk / regularInk - 1));
            require(heavierInk < mediumInk, "The interface weight is as heavy as Medium");
        }
    }
    require(kobalt::ui::interfaceFace("not a font", "Kobalt Test", "Regular", 2).isEmpty(),
            "Accepted data that is not a font");
    require(
        kobalt::ui::interfaceFace(read("IBMPlexSans-Regular.ttf").left(4096), "Kobalt Test", "Regular", 2).isEmpty(),
        "Accepted a truncated font");
    return 0;
}
