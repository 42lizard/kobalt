#include "highlight_session.h"
#include "reader_session.h"
#include "support/diagnostics.h"

#include <QPointer>
#include <QUuid>

namespace kobalt {
HighlightSession::HighlightSession(ReaderSession &model, std::function<QString()> layout, const QString &annotations,
                                   const QString &identity, const QByteArray &fingerprint, const QString &title)
    : model_(model), layout(std::move(layout)),
      store_(std::make_unique<HighlightStore>(annotations, identity, fingerprint, title)) {
    if (!store_->available()) {
        store_.reset();
        return;
    }
    loadFailed_ = !store_->load(&error_);
    if (loadFailed_) {
        diagnostic("highlights-load", {{"error", error_}}, false);
    }
}
const QVector<Highlight> &HighlightSession::highlights() const {
    static const QVector<Highlight> none;
    return store_ ? store_->highlights() : none;
}
bool HighlightSession::resolved() const {
    return store_ && !layout_.isEmpty() && layout_ == layout();
}
int HighlightSession::page(int index) const {
    return resolved() && index >= 0 ? pages_.value(index * 2, -1) : -1;
}
int HighlightSession::endPage(int index) const {
    return resolved() && index >= 0 ? pages_.value(index * 2 + 1, -1) : -1;
}
bool HighlightSession::sync() {
    if (!store_ || loadFailed_ || model_.busy() || model_.pages() <= 0) {
        return false;
    }
    const auto key = layout();
    if (layout_ == key) {
        return true;
    }
    if (lookup_ == key) {
        return false;
    }
    lookup_ = key;
    QStringList positions;
    for (const auto &h : highlights()) {
        positions << h.start << h.end;
    }
    QPointer<HighlightSession> guard(this);
    model_.bookmarkPages(
        positions,
        [guard, key, count = positions.size(), generation = generation_](QVector<int> pages) {
            if (!guard || guard->lookup_ != key) {
                return;
            }
            guard->lookup_.clear();
            if (guard->generation_ == generation && guard->layout() == key && pages.size() == count) {
                guard->layout_ = key;
                guard->pages_ = std::move(pages);
            }
            if (guard->changed) {
                guard->changed();
            }
        },
        true);
    return false;
}
bool HighlightSession::persist(QVector<Highlight> highlights) {
    if (!writable()) {
        if (failed) {
            failed();
        }
        return false;
    }
    const auto previous = store_->highlights();
    store_->setHighlights(std::move(highlights));
    if (!store_->save(&error_)) {
        store_->setHighlights(previous);
        diagnostic("highlights-save", {{"error", error_}}, false);
        if (failed) {
            failed();
        }
        return false;
    }
    error_.clear();
    ++generation_;
    layout_.clear();
    pages_.clear();
    if (changed) {
        changed();
    }
    return true;
}
void HighlightSession::save(const QString &start, const QString &end, const QString &id,
                            std::function<void(bool)> finished, const QString &chosenEnd, bool sentenceEndPolicy,
                            const QString &chosenStart, bool sentenceStartPolicy, const QString &color, const QString &style) {
    if (!writable()) {
        if (failed) {
            failed();
        }
        if (finished) {
            finished(false);
        }
        return;
    }
    if ((!color.isEmpty() && !isHighlightColor(color)) || (!style.isEmpty() && !isHighlightStyle(style))) {
        error_ = QStringLiteral("invalid-appearance");
        if (failed) {
            failed();
        }
        if (finished) {
            finished(false);
        }
        return;
    }
    QPointer<HighlightSession> guard(this);
    const auto generation = generation_;
    model_.textRange(start, end,
                     [guard, id, generation, finished, chosenEnd, sentenceEndPolicy, chosenStart, sentenceStartPolicy,
                      color, style](TextRange range) {
                         if (!guard) {
                             return;
                         }
                         if (range.text.isEmpty() || guard->generation_ != generation) {
                             if (finished) {
                                 finished(false);
                             }
                             return;
                         }
                         auto list = guard->highlights();
                         int index = -1;
                         for (int i = 0; i < list.size(); ++i) {
                             if (list[i].id == id) {
                                 index = i;
                                 break;
                             }
                         }
                         if (!id.isEmpty() && index < 0) {
                             if (finished) {
                                 finished(false);
                             }
                             return;
                         }
                         Highlight h{id.isEmpty() ? QUuid::createUuid().toString().mid(1, 36) : id,
                                     range.start,
                                     range.end,
                                     range.text,
                                     {},
                                     QDateTime::currentDateTimeUtc()};
                         h.color = color.isEmpty() ? (index < 0 ? QStringLiteral("gray") : list[index].color) : color;
                         h.style = style.isEmpty() ? (index < 0 ? QStringLiteral("shaded") : list[index].style) : style;
                         h.chosenEnd = sentenceEndPolicy ? chosenEnd : QString();
                         h.sentenceEndPolicy = sentenceEndPolicy;
                         h.chosenStart = sentenceStartPolicy ? chosenStart : QString();
                         h.sentenceStartPolicy = sentenceStartPolicy;
                         QVector<QPair<QString, QString>> others;
                         for (const auto &item : list) {
                             if (item.id != id)
                                 others.append({item.start, item.end});
                         }
                         guard->model_.intersectingRanges(range.start, range.end, others,
                             [guard, list, h, index, generation, finished](QVector<int> hits) mutable {
                                 if (!guard)
                                     return;
                                 if (guard->generation_ != generation || !hits.isEmpty()) {
                                     if (!hits.isEmpty()) {
                                         guard->error_ = QStringLiteral("overlap");
                                         if (guard->failed)
                                             guard->failed();
                                     }
                                     if (finished)
                                         finished(false);
                                     return;
                                 }
                                 // Chapter lookup follows the start endpoint, even after a cross-page selection.
                                 const auto positions = QStringList{h.start};
                                 guard->model_.bookmarkPages(
                                     positions, [guard, list, h, index, generation, finished](QVector<int> pages) mutable {
                                         if (!guard) {
                                             return;
                                         }
                                         if (pages.size() != 1 || guard->generation_ != generation) {
                                             if (finished) {
                                                 finished(false);
                                             }
                                             return;
                                         }
                                         for (const auto &chapter : guard->model_.chapters()) {
                                             if (chapter.page >= 0 && chapter.page <= pages[0]) {
                                                 h.chapter = chapter.title.left(512);
                                             }
                                         }
                                         if (index < 0) {
                                             list.append(h);
                                         } else {
                                             h.created = list[index].created;
                                             list[index] = h;
                                         }
                                         const bool saved = guard->persist(std::move(list));
                                         if (finished) {
                                             finished(saved);
                                         }
                                     });
                             });
                     });
}
bool HighlightSession::recolor(const QString &id, const QString &color) {
    return updateAppearance(id, color, {});
}
bool HighlightSession::restyle(const QString &id, const QString &style) {
    return updateAppearance(id, {}, style);
}
bool HighlightSession::updateAppearance(const QString &id, const QString &color, const QString &style) {
    if ((color.isEmpty() && style.isEmpty()) || (!color.isEmpty() && !isHighlightColor(color)) ||
        (!style.isEmpty() && !isHighlightStyle(style))) {
        error_ = QStringLiteral("invalid-appearance");
        if (failed)
            failed();
        return false;
    }
    auto list = highlights();
    for (auto &h : list) {
        if (h.id != id)
            continue;
        const QString chosenColor = color.isEmpty() ? h.color : color;
        const QString chosenStyle = style.isEmpty() ? h.style : style;
        if (h.color == chosenColor && h.style == chosenStyle && writable()) {
            error_.clear();
            return true;
        }
        h.color = chosenColor;
        h.style = chosenStyle;
        return persist(std::move(list));
    }
    return false;
}
bool HighlightSession::remove(const QString &id) {
    auto list = highlights();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == id) {
            list.removeAt(i);
            return persist(std::move(list));
        }
    }
    return false;
}
}
