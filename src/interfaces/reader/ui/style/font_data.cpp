#include "font_data.h"
#include <QVector>
#include <algorithm>
#include <cmath>

namespace kobalt::ui {
namespace {
constexpr quint32 tag(const char (&name)[5]) {
    return quint32(uchar(name[0])) << 24 | quint32(uchar(name[1])) << 16 | quint32(uchar(name[2])) << 8 |
           uchar(name[3]);
}
// Big-endian reads. A read past the end returns zero and clears `ok`.
struct Reader {
    const QByteArray &bytes;
    mutable bool ok = true;
    bool has(qint64 offset, qint64 size) const {
        return offset >= 0 && size >= 0 && offset + size <= bytes.size();
    }
    uint u8(qint64 at) const {
        if (!has(at, 1)) {
            ok = false;
            return 0;
        }
        return uchar(bytes.at(int(at)));
    }
    uint u16(qint64 at) const {
        return u8(at) << 8 | u8(at + 1);
    }
    int s16(qint64 at) const {
        return qint16(quint16(u16(at)));
    }
    quint32 u32(qint64 at) const {
        return quint32(u16(at)) << 16 | u16(at + 2);
    }
};
void put16(QByteArray &out, uint value) {
    out.append(char(value >> 8));
    out.append(char(value));
}
void put32(QByteArray &out, quint32 value) {
    put16(out, value >> 16);
    put16(out, value & 0xffff);
}
void set16(QByteArray &out, int at, uint value) {
    out[at] = char(value >> 8);
    out[at + 1] = char(value);
}
void set32(QByteArray &out, int at, quint32 value) {
    set16(out, at, value >> 16);
    set16(out, at + 2, value & 0xffff);
}
void pad(QByteArray &out) {
    while (out.size() % 4)
        out.append('\0');
}
quint32 checksum(const QByteArray &data) {
    quint32 sum = 0;
    for (int at = 0; at < data.size(); at += 4) {
        quint32 word = 0;
        for (int index = at; index < at + 4; ++index)
            word = word << 8 | (index < data.size() ? uchar(data.at(index)) : 0);
        sum += word;
    }
    return sum;
}
struct Table {
    quint32 tag;
    QByteArray data;
};
QByteArray *find(QVector<Table> &tables, quint32 name) {
    for (auto &table : tables)
        if (table.tag == name)
            return &table.data;
    return nullptr;
}

// Replace every name that carries the family, so no record still matches the
// original family in Qt's or fontconfig's list of alternative names.
bool rename(QByteArray &table, const QString &family, const QString &style) {
    const Reader in{table};
    const uint count = in.u16(2), storage = in.u16(4);
    const QString full = family + ' ' + style;
    const QString postscript = QString(family).remove(' ') + '-' + QString(style).remove(' ');
    QByteArray records, strings;
    uint kept = 0;
    for (uint index = 0; index < count; ++index) {
        const int at = 6 + 12 * index;
        const uint platform = in.u16(at), encoding = in.u16(at + 2), language = in.u16(at + 4), id = in.u16(at + 6);
        const uint length = in.u16(at + 8), offset = in.u16(at + 10);
        if (!in.ok || !in.has(storage + offset, length))
            return false;
        // Language-tag records exist only in format 1 and are not carried over.
        if (language >= 0x8000)
            continue;
        QByteArray text = table.mid(int(storage + offset), int(length));
        const QString replacement = id == 1 || id == 16 || id == 21  ? family
                                    : id == 3 || id == 4 || id == 18 ? full
                                    : id == 6                        ? postscript
                                                                     : QString();
        if (!replacement.isEmpty()) {
            if (platform == 0 || platform == 3) {
                text.clear();
                for (const QChar character : replacement)
                    put16(text, character.unicode());
            } else if (platform == 1)
                text = replacement.toLatin1();
            else
                continue;
        }
        put16(records, platform);
        put16(records, encoding);
        put16(records, language);
        put16(records, id);
        put16(records, uint(text.size()));
        put16(records, uint(strings.size()));
        strings += text;
        ++kept;
    }
    if (int(6 + 12 * kept) + strings.size() > 0xffff)
        return false;
    table.clear();
    put16(table, 0);
    put16(table, kept);
    put16(table, 6 + 12 * kept);
    table += records;
    table += strings;
    return true;
}

struct Point {
    double x, y;
};
// FT_Outline_EmboldenXY in floating point, followed by the half-strength
// translation that font_weight.cpp applies to book text. The translation
// cancels FreeType's constant offset, so each point moves by its shift only.
void embolden(QVector<Point> &points, const QVector<int> &ends, double strength) {
    // The orientation of the whole outline, as FT_Outline_Get_Orientation.
    double area = 0;
    int first = 0;
    for (const int last : ends) {
        Point previous = points[last];
        for (int index = first; index <= last; ++index) {
            area += (points[index].y - previous.y) * (points[index].x + previous.x);
            previous = points[index];
        }
        first = last + 1;
    }
    if (area == 0)
        return;
    const bool truetype = area < 0;
    const double half = strength / 2;
    const auto original = points;
    first = 0;
    for (const int last : ends) {
        const auto next = [first, last](int index) {
            return index < last ? index + 1 : first;
        };
        Point in{0, 0}, out{0, 0}, anchor{0, 0};
        double inLength = 0, outLength = 0, anchorLength = 0;
        // j cycles through the points; i follows only when points move; k
        // marks the first moved point.
        for (int i = last, j = first, k = -1; j != i && i != k; j = next(j)) {
            if (j != k) {
                out = {original[j].x - original[i].x, original[j].y - original[i].y};
                outLength = std::hypot(out.x, out.y);
                if (outLength == 0)
                    continue;
                out = {out.x / outLength, out.y / outLength};
            } else {
                out = anchor;
                outLength = anchorLength;
            }
            if (inLength != 0) {
                if (k < 0) {
                    k = i;
                    anchor = in;
                    anchorLength = inLength;
                }
                Point shift{0, 0};
                const double turn = in.x * out.x + in.y * out.y;
                // Shift only where the contour turns by less than about 160 degrees.
                if (turn > -0.9375) {
                    const double d = turn + 1;
                    shift = {in.y + out.y, in.x + out.x};
                    if (truetype)
                        shift.x = -shift.x;
                    else
                        shift.y = -shift.y;
                    double q = out.x * in.y - out.y * in.x;
                    if (truetype)
                        q = -q;
                    // Limit the shift where short segments would collapse.
                    const double limit = std::min(inLength, outLength);
                    const double scale = half * q <= limit * d ? half / d : limit / q;
                    shift = {shift.x * scale, shift.y * scale};
                }
                for (; i != j; i = next(i))
                    points[i] = {original[i].x + shift.x, original[i].y + shift.y};
            } else {
                i = j;
            }
            in = out;
            inLength = outLength;
        }
        first = last + 1;
    }
}

int coordinate(double value) {
    return qBound(-32768, int(std::lround(value)), 32767);
}
// Thicken one simple glyph. Composite glyphs refer to simple ones and are
// kept as they are, like empty glyphs. `leftShift` is the change of xMin.
bool emboldenGlyph(const QByteArray &glyph, double strength, QByteArray &result, int &leftShift) {
    leftShift = 0;
    result = glyph;
    if (glyph.isEmpty())
        return true;
    const Reader in{glyph};
    const int contours = in.s16(0);
    if (contours <= 0)
        return in.ok;
    QVector<int> ends;
    for (int contour = 0; contour < contours; ++contour) {
        const int end = int(in.u16(10 + 2 * contour));
        if (end <= (ends.isEmpty() ? -1 : ends.last()))
            return false;
        ends.append(end);
    }
    const int count = ends.last() + 1;
    const int instructions = 10 + 2 * contours;
    int at = instructions + 2 + int(in.u16(instructions));
    QVector<uint> flags;
    while (in.ok && flags.size() < count) {
        const uint flag = in.u8(at++);
        flags.append(flag);
        for (uint repeat = flag & 8 ? in.u8(at++) : 0; repeat > 0 && flags.size() < count; --repeat)
            flags.append(flag);
    }
    // A truncated flag stream must not index a partial flag list.
    if (!in.ok || flags.size() != count)
        return false;
    QVector<Point> points(count);
    for (const bool vertical : {false, true}) {
        const uint shortBit = vertical ? 4 : 2, sameBit = vertical ? 32 : 16;
        int value = 0;
        for (int index = 0; index < count; ++index) {
            const uint flag = flags[index];
            if (flag & shortBit) {
                value += flag & sameBit ? int(in.u8(at)) : -int(in.u8(at));
                at += 1;
            } else if (!(flag & sameBit)) {
                value += in.s16(at);
                at += 2;
            }
            (vertical ? points[index].y : points[index].x) = value;
        }
    }
    if (!in.ok)
        return false;
    embolden(points, ends, strength);

    QVector<int> xs(count), ys(count);
    for (int index = 0; index < count; ++index) {
        xs[index] = coordinate(points[index].x);
        ys[index] = coordinate(points[index].y);
    }
    const auto [xMin, xMax] = std::minmax_element(xs.cbegin(), xs.cend());
    const auto [yMin, yMax] = std::minmax_element(ys.cbegin(), ys.cend());
    QByteArray out;
    put16(out, uint(contours));
    put16(out, uint(*xMin) & 0xffff);
    put16(out, uint(*yMin) & 0xffff);
    put16(out, uint(*xMax) & 0xffff);
    put16(out, uint(*yMax) & 0xffff);
    // Contour ends and hinting instructions are unchanged.
    out += glyph.mid(10, instructions + 2 + int(in.u16(instructions)) - 10);
    QVector<uint> encoded;
    QByteArray xBytes, yBytes;
    for (int index = 0; index < count; ++index) {
        // Keep the on-curve bit, and the overlap bit that only the first flag carries.
        uint flag = (flags[index] & 1) | (index == 0 ? flags[0] & 0x40 : 0);
        for (const bool vertical : {false, true}) {
            const auto &values = vertical ? ys : xs;
            const int delta = values[index] - (index ? values[index - 1] : 0);
            auto &bytes = vertical ? yBytes : xBytes;
            const uint shortBit = vertical ? 4 : 2, sameBit = vertical ? 32 : 16;
            if (delta == 0)
                flag |= sameBit;
            else if (std::abs(delta) <= 255) {
                flag |= shortBit | (delta > 0 ? sameBit : 0);
                bytes.append(char(std::abs(delta)));
            } else
                put16(bytes, uint(delta) & 0xffff);
        }
        encoded.append(flag);
    }
    for (int index = 0; index < count;) {
        int run = 1;
        while (index + run < count && run < 256 && encoded[index + run] == encoded[index])
            ++run;
        if (run > 1) {
            out.append(char(encoded[index] | 8));
            out.append(char(run - 1));
        } else
            out.append(char(encoded[index]));
        index += run;
    }
    out += xBytes;
    out += yBytes;
    leftShift = *xMin - in.s16(2);
    result = out;
    return true;
}

bool embolden(QVector<Table> &tables, int steps) {
    auto *head = find(tables, tag("head")), *maxp = find(tables, tag("maxp")), *hhea = find(tables, tag("hhea"));
    auto *hmtx = find(tables, tag("hmtx")), *loca = find(tables, tag("loca")), *glyf = find(tables, tag("glyf"));
    if (!head || !maxp || !hhea || !hmtx || !loca || !glyf)
        return false;
    const Reader headData{*head}, maxpData{*maxp}, hheaData{*hhea}, locaData{*loca};
    const int unitsPerEm = int(headData.u16(18));
    const bool longOffsets = headData.s16(50) == 1;
    const int glyphs = int(maxpData.u16(4)), metrics = int(hheaData.u16(34));
    if (!headData.ok || !maxpData.ok || !hheaData.ok || unitsPerEm == 0 || metrics < 1 || metrics > glyphs ||
        hmtx->size() < 4 * metrics + 2 * (glyphs - metrics))
        return false;
    const auto location = [&](int glyph) -> qint64 {
        return longOffsets ? locaData.u32(4 * glyph) : 2 * qint64(locaData.u16(2 * glyph));
    };
    const double strength = unitsPerEm * steps / 256.0;
    int xMin = headData.s16(36), yMin = headData.s16(38), xMax = headData.s16(40), yMax = headData.s16(42);
    QByteArray outlines, locations;
    for (int glyph = 0; glyph < glyphs; ++glyph) {
        const qint64 start = location(glyph), end = location(glyph + 1);
        if (!locaData.ok || end < start || end > glyf->size())
            return false;
        QByteArray result;
        int leftShift = 0;
        if (!emboldenGlyph(glyf->mid(int(start), int(end - start)), strength, result, leftShift))
            return false;
        put32(locations, quint32(outlines.size()));
        outlines += result;
        pad(outlines);
        if (result.size() >= 10) {
            const Reader bounds{result};
            xMin = qMin(xMin, bounds.s16(2));
            yMin = qMin(yMin, bounds.s16(4));
            xMax = qMax(xMax, bounds.s16(6));
            yMax = qMax(yMax, bounds.s16(8));
        }
        // FreeType places the origin at xMin minus the left side bearing.
        // Move the bearing with the edge so the glyph stays centred.
        if (leftShift) {
            const int at = glyph < metrics ? 4 * glyph + 2 : 4 * metrics + 2 * (glyph - metrics);
            set16(*hmtx, at, uint(Reader{*hmtx}.s16(at) + leftShift) & 0xffff);
        }
    }
    put32(locations, quint32(outlines.size()));
    *glyf = outlines;
    *loca = locations;
    set16(*head, 50, 1);
    set16(*head, 36, uint(xMin) & 0xffff);
    set16(*head, 38, uint(yMin) & 0xffff);
    set16(*head, 40, uint(xMax) & 0xffff);
    set16(*head, 42, uint(yMax) & 0xffff);
    return true;
}
}

QPainterPath weightedOutline(const QPainterPath &path, qreal strength) {
    if (strength <= 0 || path.isEmpty())
        return path;
    QVector<Point> points;
    QVector<int> ends;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (element.isMoveTo() && !points.isEmpty())
            ends.append(points.size() - 1);
        points.append({element.x, element.y});
    }
    ends.append(points.size() - 1);
    embolden(points, ends, strength);
    QPainterPath result;
    result.setFillRule(path.fillRule());
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        const auto point = points[i];
        if (element.isMoveTo())
            result.moveTo(point.x, point.y);
        else if (element.isLineTo())
            result.lineTo(point.x, point.y);
        else if (element.type == QPainterPath::CurveToElement) {
            result.cubicTo(point.x, point.y, points[i + 1].x, points[i + 1].y, points[i + 2].x, points[i + 2].y);
            i += 2;
        }
    }
    return result;
}

QByteArray interfaceFace(const QByteArray &font, const QString &family, const QString &style, int steps) {
    const Reader in{font};
    const quint32 version = in.u32(0);
    if (!in.ok || (version != 0x00010000 && version != tag("true")))
        return {};
    QVector<Table> tables;
    for (int index = 0, count = int(in.u16(4)); index < count; ++index) {
        const int at = 12 + 16 * index;
        const quint32 name = in.u32(at), offset = in.u32(at + 8), length = in.u32(at + 12);
        if (!in.ok || !in.has(offset, length))
            return {};
        // A digital signature would no longer match the changed tables.
        if (name != tag("DSIG"))
            tables.append({name, font.mid(int(offset), int(length))});
    }
    auto *head = find(tables, tag("head")), *names = find(tables, tag("name"));
    if (!head || head->size() < 54 || !names || !rename(*names, family, style))
        return {};
    if (steps > 0 && !embolden(tables, steps))
        return {};
    std::sort(tables.begin(), tables.end(), [](const Table &a, const Table &b) {
        return a.tag < b.tag;
    });

    const int count = tables.size();
    int power = 1, exponent = 0;
    while (power * 2 <= count) {
        power *= 2;
        ++exponent;
    }
    QByteArray out;
    put32(out, version);
    put16(out, uint(count));
    put16(out, uint(power * 16));
    put16(out, uint(exponent));
    put16(out, uint(count - power) * 16);
    int offset = 12 + 16 * count, headOffset = -1;
    for (auto &table : tables) {
        // The head checksum is taken with checkSumAdjustment set to zero.
        if (table.tag == tag("head")) {
            set32(table.data, 8, 0);
            headOffset = offset;
        }
        put32(out, table.tag);
        put32(out, checksum(table.data));
        put32(out, quint32(offset));
        put32(out, quint32(table.data.size()));
        offset += (table.data.size() + 3) & ~3;
    }
    for (const auto &table : tables) {
        out += table.data;
        pad(out);
    }
    set32(out, headOffset + 8, 0xB1B0AFBAu - checksum(out));
    return out;
}
}
