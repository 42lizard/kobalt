#pragma once
#include <QByteArray>
#include <QString>
#include <QPainterPath>

namespace kobalt::ui {
// Returns a copy of a TrueType font under another family name. With `steps`
// above zero, every outline is also thickened by steps/256 em, the unit of
// the reading weight setting, with the same method: FreeType's emboldening,
// centred on the glyph. Advances stay the same, so text keeps its layout.
// Returns an empty array for data that is not a TrueType font.
// Apply the same centred outline expansion to a Qt glyph path. Strength is
// in path coordinates; reading weight uses pixel size * steps / 256.
QPainterPath weightedOutline(const QPainterPath &path, qreal strength);
QByteArray interfaceFace(const QByteArray &font, const QString &family, const QString &style, int steps);
}
