#include "support/qt_compat.h"
#include "reader_model.h"
#include <lvdocview.h>
#include <QTextBoundaryFinder>

namespace kobalt {
namespace {
QString qstring(const lString32 &s) {
    return QString::fromUtf8(UnicodeToUtf8(s).c_str());
}
lString32 cre(const QString &s) {
    return Utf8ToUnicode(s.toUtf8().constData());
}
// Collapse whitespace in search context, but keep one space at an edge that
// had one. The match joins its context without guessing at word boundaries.
QString contextText(const QString &text) {
    auto result = text.simplified();
    if (result.isEmpty())
        return result;
    if (text.at(0).isSpace())
        result.prepend(' ');
    if (text.at(text.size() - 1).isSpace())
        result.append(' ');
    return result;
}
}
QVector<int> ReaderModel::pagesOf(const QStringList &positions) const {
    QVector<int> pages;
    if (!engine_) {
        pages.fill(-1, positions.size());
        return pages;
    }
    auto *doc = engine_->getDocument();
    for (const auto &position : positions) {
        const auto pointer = position.isEmpty() ? ldomXPointer() : doc->createXPointer(cre(position));
        pages.append(pointer.isNull() ? -1 : engine_->getBookmarkPage(pointer));
    }
    return pages;
}
QString ReaderModel::pageStart(int page) const {
    if (!engine_ || page < 0 || page >= pages())
        return {};
    return qstring(engine_->getPageBookmark(page).toString());
}
QString ReaderModel::pageExcerpt(int page) const {
    if (!engine_ || page < 0 || page >= pages())
        return {};
    return qstring(engine_->getPageText(false, page)).simplified().left(200);
}
WordSelection ReaderModel::wordAt(QPoint point) {
    WordSelection result;
    if (!engine_ || !QRect(QPoint(), size_).contains(point))
        return result;
    ldomXPointerEx hit(engine_->getNodeByPoint(lvPoint(point.x(), point.y()), true));
    if (hit.isNull() || !hit.isText() || !hit.isVisible())
        return result;

    // Collect a bounded window in this rendered block, retaining a pointer for
    // every UTF-16 unit. Inline markup must not introduce word boundaries.
    auto previous = [](ldomXPointerEx &p) {
        return p.prevVisibleChar(true);
    };
    auto next = [](ldomXPointerEx &p) {
        if (!p.nextVisibleChar(true))
            return false;
        if (p.getOffset() == p.getNode()->getText().length())
            return p.nextVisibleChar(true);
        return true;
    };
    if (hit.getOffset() >= hit.getNode()->getText().length() && !previous(hit))
        return result;
    auto first = hit;
    for (int i = 0; i < 256; ++i) {
        auto p = first;
        if (!previous(p))
            break;
        first = p;
    }
    QString text;
    QVector<ldomXPointerEx> positions;
    int offset = -1;
    auto cursor = first;
    for (int count = 0; count < 513; ++count) {
        const auto nodeText = cursor.getNode()->getText();
        if (cursor.getOffset() < 0 || cursor.getOffset() >= nodeText.length())
            break;
        if (cursor == hit)
            offset = text.size();
        const char32_t character = nodeText[cursor.getOffset()];
        // A discretionary hyphen is layout, not part of the dictionary key.
        if (character != 0xad) {
            const auto piece = kobalt::qt::character(character);
            text += piece;
            for (int i = 0; i < piece.size(); ++i)
                positions.append(cursor);
        }
        if (!next(cursor))
            break;
    }
    if (offset < 0 || offset >= text.size())
        return result;
    QTextBoundaryFinder words(QTextBoundaryFinder::Word, text);
    words.setPosition(offset);
    // Qt reports Han and Thai boundaries without word-item flags. Keep
    // these boundaries so their text can still be selected. Thai selection
    // follows these boundaries; it does not use a dictionary segmenter.
    const auto codepoints = text.mid(offset).toUcs4();
    const auto script = codepoints.isEmpty() ? QChar::Script_Unknown : QChar::script(codepoints.first());
    const bool unflaggedScript = script == QChar::Script_Han || script == QChar::Script_Thai;
    if (!(words.boundaryReasons() & QTextBoundaryFinder::StartOfItem) && !(unflaggedScript && words.isAtBoundary())) {
        if (words.toPreviousBoundary() < 0)
            return result;
    }
    if (!(words.boundaryReasons() & QTextBoundaryFinder::StartOfItem) && !unflaggedScript)
        return result;
    const int begin = words.position();
    const int end = words.toNextBoundary();
    if (end <= offset || end - begin > 256 ||
        (!(words.boundaryReasons() & QTextBoundaryFinder::EndOfItem) && !unflaggedScript))
        return result;
    // Do not silently truncate a word at the bounded window's edges.
    auto before = first;
    auto after = positions.back();
    if ((begin == 0 && previous(before)) || (end == text.size() && next(after)))
        return result;
    auto startPointer = positions[begin], endPointer = positions[end - 1];
    endPointer.setOffset(endPointer.getOffset() + 1);
    ldomXRange range(startPointer, endPointer);
    LVArray<lvRect> rectangles;
    range.getSegmentRects(rectangles);
    bool contains = false;
    for (int i = 0; i < rectangles.length(); ++i) {
        const auto &r = rectangles[i];
        lvPoint top(r.left, r.top), bottom(r.right, r.bottom);
        if (!engine_->docToWindowPoint(top) || !engine_->docToWindowPoint(bottom, true))
            continue;
        const QRect rect(qMin(top.x, bottom.x), qMin(top.y, bottom.y), qAbs(bottom.x - top.x), qAbs(bottom.y - top.y));
        const auto clipped = rect.intersected(QRect(QPoint(), size_));
        if (clipped.isEmpty())
            continue;
        result.rectangles.append(clipped);
        contains |= clipped.contains(point);
    }
    if (!contains)
        return {};
    result.text = text.mid(begin, end - begin);
    result.start = qstring(startPointer.toString());
    result.end = qstring(endPointer.toString());
    return result;
}
QVector<QRect> ReaderModel::rangeRects(const QString &start, const QString &end, bool includeImages) {
    QVector<QRect> result;
    if (!engine_)
        return result;
    auto *doc = engine_->getDocument();
    ldomXPointerEx a(doc->createXPointer(cre(start))), b(doc->createXPointer(cre(end)));
    if (a.isNull() || b.isNull())
        return result;
    ldomXRange range(a, b);
    LVArray<lvRect> rectangles;
    range.getSegmentRects(rectangles, includeImages);
    for (int i = 0; i < rectangles.length(); ++i) {
        const auto &r = rectangles[i];
        lvPoint top(r.left, r.top), bottom(r.right, r.bottom);
        if (!engine_->docToWindowPoint(top) || !engine_->docToWindowPoint(bottom, true))
            continue;
        const auto clipped =
            QRect(qMin(top.x, bottom.x), qMin(top.y, bottom.y), qAbs(bottom.x - top.x), qAbs(bottom.y - top.y))
                .intersected(QRect(QPoint(), size_));
        if (!clipped.isEmpty())
            result.append(clipped);
    }
    return result;
}
SearchBatch ReaderModel::searchPage(const QString &query, int number) {
    SearchBatch result;
    result.nextPage = number + 1;
    result.done = !engine_ || number < 0 || number + 1 >= pages() || query.isEmpty() || query.size() > 128;
    if (!engine_ || number < 0 || number >= pages() || query.isEmpty() || query.size() > 128)
        return result;
    auto *doc = engine_->getDocument();
    ldomXPointerEx start(engine_->getPageBookmark(number));
    ldomXPointerEx end = number + 1 < pages() ? ldomXPointerEx(engine_->getPageBookmark(number + 1))
                                              : ldomXRange(doc->getRootNode(), true).getEnd();
    if (start.isNull() || end.isNull())
        return result;
    // Include the query's tail past the next page boundary. Search still owns
    // only matches beginning on this page, so wrapped phrases appear once.
    // Extra code points cover canonical decomposition and case folding.
    auto extended = end;
    for (int remaining = query.size() * 4 + 16; remaining > 0;) {
        auto next = extended;
        if (!next.nextVisibleChar(true))
            break;
        extended = next;
        const auto text = extended.getNode()->getText();
        if (extended.getOffset() >= text.length())
            continue;
        const char32_t c = text[extended.getOffset()];
        if (c != 0xad && !QChar::isSpace(c))
            --remaining;
    }
    ldomXRange range(start, extended);
    ldomXRangeList matches;
    // Compatibility normalization matches ligatures such as "ﬁ" to "fi". Folding
    // hyphens and skipping format controls matches "e‑mail" (a non-breaking
    // hyphen) and words split by zero-width characters as typed.
    const auto flags = LDOM_FIND_TEXT_MATCH_ACROSS_TEXT_NODES | LDOM_FIND_TEXT_COLLAPSE_CONSECUTIVE_SPACES |
                       LDOM_FIND_TEXT_NORMALIZE_CANONICAL | LDOM_FIND_TEXT_NORMALIZE_COMPATIBILITY |
                       LDOM_FIND_TEXT_IGNORE_FORMAT_CONTROL_CHARS | LDOM_FIND_TEXT_FOLD_SPACES |
                       LDOM_FIND_TEXT_FOLD_APOSTROPHES | LDOM_FIND_TEXT_FOLD_HYPHENS;
    range.findText(cre(query), true, false, matches, 101, 0, -1, false, false, flags);
    for (int i = 0; i < matches.length(); ++i) {
        auto &match = *matches[i];
        const int page = engine_->getBookmarkPage(match.getStart());
        if (page != number)
            continue;
        if (result.hits.size() == 100) {
            result.limited = result.done = true;
            break;
        }
        auto before = match.getStart(), after = match.getEnd();
        for (int n = 0; n < 48; ++n)
            if (!before.prevVisibleChar(true))
                break;
        for (int n = 0; n < 96; ++n)
            if (!after.nextVisibleChar(true))
                break;
        ldomXRange prefix(before, match.getStart()), suffix(match.getEnd(), after);
        result.hits.append({qstring(match.getStart().toString()), qstring(match.getEnd().toString()),
                            contextText(qstring(prefix.getRangeText())), qstring(match.getRangeText()).simplified(),
                            contextText(qstring(suffix.getRangeText())), page});
    }
    return result;
}
}
