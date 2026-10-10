#include "text.h"
#include "../style/fonts.h"
#include "../style/font_data.h"
#include "../style/styles.h"
#include "../style/theme.h"
#include <QAbstractTextDocumentLayout>
#include <QFontInfo>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

namespace kobalt::ui {
namespace m = metrics;
void WholeLineLabel::paintEvent(QPaintEvent *) {
    QTextDocument document;
    document.setDefaultFont(font());
    document.setDocumentMargin(0);
    document.setHtml(text());
    document.setTextWidth(width());
    document.documentLayout()->documentSize();
    qreal bottom = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const auto *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const auto line = layout->lineAt(i);
            const qreal end = layout->position().y() + line.y() + line.height();
            if (end <= height())
                bottom = qMax(bottom, end);
        }
    }
    QPainter painter(this);
    const QRectF clip(0, 0, width(), bottom);
    if (alignment() & Qt::AlignVCenter)
        painter.translate(0, (height() - bottom) / 2);
    painter.setClipRect(clip);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.clip = clip;
    document.documentLayout()->draw(&painter, context);
}
QVector<QPair<int, int>> samplePrefixes(const QString &text) {
    const auto letters = text.toUcs4();
    auto letter = [](uint ch) {
        const auto script = QChar::script(ch);
        const bool supported = script == QChar::Script_Latin || script == QChar::Script_Greek ||
                               script == QChar::Script_Cyrillic || script == QChar::Script_Hebrew;
        const auto category = QChar::category(ch);
        return supported && category >= QChar::Letter_Uppercase && category <= QChar::Letter_Other;
    };
    auto mark = [](uint ch) {
        const auto category = QChar::category(ch);
        return category >= QChar::Mark_NonSpacing && category <= QChar::Mark_Enclosing;
    };
    auto word = [&](uint ch) {
        return letter(ch) || mark(ch) || QChar::category(ch) == QChar::Number_DecimalDigit || ch == '\'' ||
               ch == 0x2019 || ch == 0x00ad;
    };
    QVector<int> offsets;
    int offset = 0;
    for (const auto ch : letters) {
        offsets.append(offset);
        offset += ch > 0xffff ? 2 : 1;
    }
    offsets.append(offset);
    QVector<QPair<int, int>> result;
    for (int i = 0; i < letters.size();) {
        if (!letter(letters[i])) {
            ++i;
            continue;
        }
        const int start = i;
        int end = i + 1, count = 1;
        while (end < letters.size() && word(letters[end])) {
            if (letter(letters[end]))
                ++count;
            ++end;
        }
        int prefix = start, remaining = (count + 1) / 2;
        while (prefix < end && remaining > 0) {
            if (letter(letters[prefix]))
                --remaining;
            ++prefix;
        }
        while (prefix < end && mark(letters[prefix]))
            ++prefix;
        result.append({offsets[start], offsets[prefix] - offsets[start]});
        i = end;
    }
    return result;
}

void drawOpticalText(QPainter &painter, const QRectF &area, const QString &text, bool centered,
                     SampleAppearance appearance) {
    const bool bold = appearance.bionic == "bold";
    const int weight = bold ? 0 : qBound(0, appearance.weight, 8);
    if (!bold && !weight) {
        const auto label = painter.fontMetrics().elidedText(text, Qt::ElideRight, qMax(0, int(area.width())));
        const auto bounds = QFontMetricsF(painter.font()).tightBoundingRect(label);
        painter.drawText(QPointF((centered ? area.center().x() - bounds.width() / 2 : area.left()) - bounds.left(),
                                 area.center().y() - bounds.center().y()),
                         label);
        return;
    }
    // Shape the full label so RTL order and combining marks survive emphasis.
    // Intrinsic width includes real bold advances before the label is elided.
    const bool prefixesEnabled = bold && !QFontInfo(painter.font()).fixedPitch();
    QString label = text;
    for (;;) {
        QTextLayout layout(label, painter.font());
        const auto prefixes = prefixesEnabled ? samplePrefixes(label) : QVector<QPair<int, int>>();
        QVector<QTextLayout::FormatRange> formats;
        if (bold)
            for (const auto &prefix : prefixes) {
                QTextLayout::FormatRange range;
                range.start = prefix.first;
                range.length = prefix.second;
                range.format.setFontWeight(QFont::Bold);
                formats.append(range);
            }
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 0)
        layout.setAdditionalFormats(formats.toList());
#else
        layout.setFormats(formats);
#endif
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        auto line = layout.createLine();
        if (line.isValid())
            line.setLineWidth(qMax(1.0, area.width()));
        layout.endLayout();
        if (!line.isValid())
            return;
        if (line.naturalTextWidth() > area.width() && label.size() > 1) {
            if (label.endsWith(QChar(0x2026)))
                label.chop(1);
            if (label.isEmpty())
                return;
            label.chop(label.at(label.size() - 1).isLowSurrogate() ? 2 : 1);
            label.append(QChar(0x2026));
            continue;
        }
        struct Glyph {
            QPainterPath path;
            QGlyphRun run;
            QRectF clip;
            bool split = false;
        };
        QVector<Glyph> glyphs;
        QRectF bounds;
        auto append = [&](int start, int length, int steps) {
            for (const auto &run : line.glyphRuns(start, length)) {
                QPainterPath path;
                path.setFillRule(Qt::WindingFill);
                const auto indexes = run.glyphIndexes();
                const auto positions = run.positions();
                for (int i = 0; i < indexes.size(); ++i) {
                    auto outline = run.rawFont().pathForGlyph(indexes[i]);
                    if (steps)
                        outline = weightedOutline(outline, run.rawFont().pixelSize() * steps / 256.0);
                    outline.translate(positions[i]);
                    path.addPath(outline);
                }
                const bool split = run.flags().testFlag(QGlyphRun::SplitLigature);
                const auto ink = path.isEmpty() ? run.boundingRect() : path.boundingRect();
                const auto clip = run.boundingRect();
                bounds = bounds.united(split ? ink.intersected(clip) : ink);
                glyphs.append({path, run, clip, split});
            }
        };
        append(0, label.size(), weight);
        const QPointF origin((centered ? area.center().x() - bounds.width() / 2 : area.left()) - bounds.left(),
                             area.center().y() - bounds.center().y());
        painter.save();
        painter.translate(origin);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto ink = painter.pen().color();
        for (const auto &glyph : glyphs) {
            painter.save();
            if (glyph.split)
                painter.setClipRect(glyph.clip, Qt::IntersectClip);
            if (glyph.path.isEmpty())
                painter.drawGlyphRun(QPointF(), glyph.run);
            else
                painter.fillPath(glyph.path, ink);
            painter.restore();
        }
        painter.restore();
        return;
    }
}
void SampleLabel::paintEvent(QPaintEvent *event) {
    if (!optical) {
        QLabel::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    if (wordWrap() && painter.fontMetrics().boundingRect(text()).width() > width()) {
        // Keep whole words together. Balance two lines before eliding a word
        // that cannot fit, using the same optical painter for weight previews.
        const QFontMetricsF metrics(font());
        int split = -1;
        qreal best = -1;
        for (int i = 1; i < text().size() - 1; ++i) {
            if (!text().at(i).isSpace())
                continue;
            const auto first = text().left(i).trimmed(), second = text().mid(i + 1).trimmed();
            if (first.isEmpty() || second.isEmpty())
                continue;
            const qreal widest = qMax(metrics.boundingRect(first).width(), metrics.boundingRect(second).width());
            if (best < 0 || widest < best) {
                best = widest;
                split = i;
            }
        }
        const qreal lineHeight = qMax(metrics.tightBoundingRect(text()).height(),
                                      metrics.tightBoundingRect(QStringLiteral("Hox")).height()) +
                                 px(5);
        if (split >= 0 && lineHeight * 2 <= height()) {
            const qreal top = (height() - lineHeight * 2) / 2;
            drawOpticalText(painter, QRectF(0, top, width(), lineHeight), text().left(split).trimmed(),
                            alignment().testFlag(Qt::AlignHCenter), appearance);
            drawOpticalText(painter, QRectF(0, top + lineHeight, width(), lineHeight), text().mid(split + 1).trimmed(),
                            alignment().testFlag(Qt::AlignHCenter), appearance);
            return;
        }
    }
    drawOpticalText(painter, rect(), text(), alignment().testFlag(Qt::AlignHCenter), appearance);
}
// Some fonts report an x-height that does not match their glyphs. Measure
// capitals as well as lowercase so small x-heights do not inflate font names.
QFont fontSample(const QString &face) {
    QFont font(face.isEmpty() ? kobalt::ui::interfaceFontFamily() : face);
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPixelSize(128);
    const auto resolved = QFontInfo(font).family();
    static QHash<QString, int> sizes;
    auto found = sizes.constFind(resolved);
    if (found == sizes.cend()) {
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setFont(font);
            painter.setPen(Qt::black);
            painter.drawText(QPointF(16, 180), QStringLiteral("Hox"));
        }
        int first = image.height(), last = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qGray(image.pixel(x, y)) < 128) {
                    first = qMin(first, y);
                    last = y;
                    break;
                }
        const int size =
            last >= first ? qRound(128.0 * m::choices::sampleInkHeight / (last - first + 1)) : m::choices::sampleBase;
        found = sizes.insert(resolved, qBound<int>(m::choices::sampleMinimum, size, m::choices::sampleMaximum));
    }
    font.setPixelSize(*found);
    return font;
}

void captionLabel(QLabel *label, int pixels) {
    label->setFont(captionFont(pixels));
    label->setTextFormat(Qt::PlainText);
    readerStyle(label, styles::caption(pixels));
}

}
