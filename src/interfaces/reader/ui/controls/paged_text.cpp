#include "../style/fonts.h"
#include "paged_text.h"
#include "../style/metrics.h"
#include "../style/theme.h"
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextCursor>
#include <QTextLayout>
#include <QTextTable>
#include <cmath>

namespace kobalt::ui {
PagedText::PagedText(QWidget *parent) : QWidget(parent) {
    const int margin = metrics::acknowledgments::textMargin;
    margins = QMargins(margin, margin, margin, margin);
    setAttribute(Qt::WA_OpaquePaintEvent);
    QFont font = kobalt::ui::interfaceFont();
    font.setPixelSize(metrics::text::body);
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    document.setUndoRedoEnabled(false);
    auto option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
}
void PagedText::setHtml(const QString &html) {
    source = html;
    insertedBreaks.clear();
    document.setHtml(html);
    current = 0;
    paginate();
    update();
}
void PagedText::setTextFont(const QFont &font) {
    document.setDefaultFont(font);
    paginate();
    current = qBound(0, current, pages() - 1);
    update();
}
void PagedText::changeEvent(QEvent *event) {
    // The document keeps the faces of its font, and on Qt 5.2 its laid-out
    // text keeps them too. Build the text again with the renewed font. See
    // refreshInterfaceText().
    if (event->type() == QEvent::FontChange) {
        const int page = current;
        document.setDefaultFont(renewedFont(document.defaultFont()));
        insertedBreaks.clear();
        document.setHtml(source);
        paginate();
        current = qBound(0, page, pages() - 1);
        update();
    }
    QWidget::changeEvent(event);
}
void PagedText::setMargins(const QMargins &value) {
    margins = value;
    paginate();
    current = qBound(0, current, pages() - 1);
    update();
}
int PagedText::pages() const {
    return qMax(1, document.pageCount());
}
void PagedText::setPage(int number) {
    current = qBound(0, number, pages() - 1);
    update();
}
void PagedText::resizeEvent(QResizeEvent *) {
    paginate();
    current = qBound(0, current, pages() - 1);
}
void PagedText::paginate() {
    // Let Qt break rich-text blocks and tables across pages. Merging overlapping
    // line rectangles can turn a whole table into one block and shrink its text.
    for (const auto &entry : insertedBreaks) {
        QTextCursor cursor(entry.first);
        cursor.setBlockFormat(entry.second);
    }
    insertedBreaks.clear();
    const qreal height = qMax(1, this->height() - margins.top() - margins.bottom());
    document.setPageSize(QSizeF(qMax(1, width() - margins.left() - margins.right()), height));
    // Kobo's Qt patch skips the normal page-boundary check for the first line
    // of each block. Break before lines that straddle a page or follow a margin
    // onto the next page. Missing the latter leaves Qt 5's page boundary behind
    // the text, so a later wrapped line jumps backward and overlaps earlier text.
    // Each block gets at most one added break; later lines use Qt's pagination.
    for (int pass = 0; pass < document.blockCount(); ++pass) {
        document.size();
        bool changed = false;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            const auto *layout = block.layout();
            if (!layout || !layout->lineCount())
                continue;
            const auto line = layout->lineAt(0);
            const qreal top = document.documentLayout()->blockBoundingRect(block).top() + line.y();
            const qreal bottom = top + line.height();
            const auto format = block.blockFormat();
            bool skippedBoundary = false;
            const auto previous = block.previous();
            if (previous.isValid() && previous.layout() && previous.layout()->lineCount()) {
                const QTextCursor cursor(block), previousCursor(previous);
                const auto *table = cursor.currentTable();
                // Document order crosses table cells, which paginate independently.
                const bool sameFlow = cursor.currentFrame() == previousCursor.currentFrame() &&
                                      (!table || table->cellAt(cursor) == table->cellAt(previousCursor));
                const auto last = previous.layout()->lineAt(previous.layout()->lineCount() - 1);
                const qreal previousTop = document.documentLayout()->blockBoundingRect(previous).top() + last.y();
                skippedBoundary = sameFlow && std::floor(top / height) > std::floor(previousTop / height);
            }
            if (line.height() > height || format.pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore) ||
                (!skippedBoundary && std::floor(top / height) == std::floor((bottom - .01) / height)))
                continue;
            insertedBreaks.append({block, format});
            auto next = format;
            next.setPageBreakPolicy(format.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
            QTextCursor cursor(block);
            cursor.setBlockFormat(next);
            document.size();
            changed = true;
        }
        if (!changed)
            break;
    }
}
void PagedText::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    const auto pageSize = document.pageSize();
    const qreal start = current * pageSize.height();
    painter.translate(margins.left(), margins.top() - start);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.clip = QRectF(QPointF(0, start), pageSize);
    // Qt 6 draws <hr> in the inactive window text colour and Qt 5.2 in Dark.
    // Both become the light row divider, which leaves the text colour alone.
    context.palette.setColor(QPalette::Inactive, QPalette::WindowText, dividerColor(this));
    context.palette.setColor(QPalette::Dark, dividerColor(this));
    painter.setClipRect(context.clip);
    document.documentLayout()->draw(&painter, context);
}
}
