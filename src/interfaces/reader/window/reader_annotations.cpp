#include "../ui/controls/highlight_palette.h"
#include "reader_p.h"

#include "interfaces/reader/ui/style/styles.h"
#include "platform/api/kobo_refresh.h"
#include "support/qt_font.h"
#include "support/strings.h"

#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <limits>
#include <utility>

namespace kobalt::reader {
namespace m = kobalt::ui::metrics;
namespace strings = kobalt::strings;

void Reader::paintAnnotationDraft() {
    page->annotate(annotationMode ? draftHighlightRects : QVector<QRect>{}, savedHighlightRects, {},
                   draftHighlightVertical);
}

void Reader::buildAnnotations() {
    annotationHeader = new ui::Panel(this, Qt::BottomEdge);
    annotationHeader->setObjectName("rrAnnotationHeader");
    annotationHeader->setEdgeWidth(m::chrome::heavyRule);
    annotationHeader->setFill(QPalette::WindowText);
    auto *top = new QHBoxLayout(annotationHeader);
    top->setContentsMargins(0, 0, 0, m::chrome::heavyRule);
    top->setSpacing(0);
    annotationHint = new ui::ModeBanner(strings::get("annotations.mode_active"), strings::get("annotations.tap_start"),
                                        annotationHeader);
    annotationHint->setObjectName("rrAnnotationBanner");
    annotationHint->titleLabel()->setObjectName("rrAnnotationModeTitle");
    annotationHint->helperLabel()->setObjectName("rrAnnotationHint");
    top->addWidget(annotationHint, 1);
    auto *close = new ui::CellButton(ui::ControlIcon::Close, annotationHeader, ui::px(48));
    close->setObjectName("rrAnnotationClose");
    close->setInverted(true);
    close->setAccessibleName(strings::get("annotations.close"));
    ui::readerStyle(close, "QPushButton{border:none;padding:0;min-width:0;min-height:0;}");
    close->setRule(Qt::LeftEdge);
    close->setFixedSize(m::chrome::cellWidth, ui::px(72) - m::chrome::heavyRule);
    top->addWidget(close);
    connect(close, &QPushButton::clicked, this, [this] {
        leaveAnnotations();
    });
    annotationPopover = new ui::ModalOverlay(this);
    annotationPopover->setObjectName("rrAnnotationPopover");
    annotationPopover->setInputBarrier(false);
    annotationPopover->dismissed = [this] {
        ++annotationEpoch;
        annotationPopover->hide();
        paintAnnotationDraft();
    };
    auto *rows = annotationPopover->card()->rows();
    rows->setContentsMargins(m::control::border, m::control::border, m::control::border, m::control::border);
    rows->setSpacing(0);
    annotationHeader->hide();
    annotationPopover->hide();
    annotationHint->contentChanged = [this] {
        layoutAnnotationHeader();
    };
}

void Reader::enterAnnotations() {
    if (model->busy() || !highlights->writable() || appearanceEdit.panelOpen() || general->isVisible()) {
        return;
    }
    dismissLookup();
    dismissControls();
    annotationMode = true;
    syncAnnotationHint();
    annotationHeader->setVisible(!minimalAnnotationMode());
    syncAnnotations();
}

void Reader::leaveAnnotations() {
    const bool discardSelection = !annotationStart.isEmpty() || !annotationEditing.isEmpty();
    annotationMode = false;
    annotationColor = annotationSelectedColor;
    annotationColorChosen = false;
    annotationAdjusting = false;
    annotationDirty = false;
    annotationActionAnchor = {};
    annotationShowActionsPending = false;
    ++annotationEpoch;
    ++annotationSaveRevision;
    annotationStart.clear();
    annotationEnd.clear();
    annotationEditing.clear();
    syncMarkupRequest();
    // Discarding a selection clears draft geometry and restores any saved range
    // hidden by editing. Ordinary dismissal keeps the resolved saved marks.
    if (discardSelection) {
        annotationFrame.clear();
    }
    annotationHeader->hide();
    annotationPopover->hide();
    page->annotate({}, savedHighlightRects);
    refresh();
}

void Reader::finishAnnotations() {
    if (annotationSaving) {
        return;
    }
    if (annotationStart.isEmpty() || !annotationDirty) {
        leaveAnnotations();
        return;
    }
    QVector<AnnotationAction> actions;
    if (!annotationEnd.isEmpty()) {
        actions.append(
            {"rrAnnotationSave", ui::ControlIcon::SaveHighlight, strings::get("annotations.retry_save"), [this] {
                 prepareAnnotation(true);
             }});
    }
    actions.append(
        {"rrAnnotationDiscard", ui::ControlIcon::ClearSelection, strings::get("annotations.discard_and_stop"), [this] {
             leaveAnnotations();
         }});
    annotationActionsFor(actions, annotationHeader->geometry(), false);
}

void Reader::annotationActionsFor(const QVector<AnnotationAction> &actions, QRect anchor, bool includeStop,
                                  const QString &paletteColor, std::function<void(QString)> colorChanged) {
    paintAnnotationDraft();
    auto *card = annotationPopover->card();
    auto *rows = card->rows();
    while (auto *item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int desiredWidth = m::links::popupWidth;
    if (colorAnnotations() && colorChanged) {
        auto *palette = new ui::HighlightPalette(paletteColor, colorChanged, card);
        rows->addWidget(palette);
        palette->show();
        desiredWidth = qMax(desiredWidth, ui::px(620));
    }
    auto menuActions = actions;
    if (annotationMode && !annotationStart.isEmpty()) {
        menuActions.append({"rrAnnotationCancel", ui::ControlIcon::ClearSelection,
                            strings::get("annotations.cancel_selection"), [this] {
                                clearAnnotationDraft();
                            }});
    }
    if (includeStop && annotationMode) {
        menuActions.append({"rrAnnotationStop", ui::ControlIcon::Back, strings::get("annotations.stop"), [this] {
                                const auto epoch = annotationEpoch;
                                // The exit guard replaces this row. Let the proxy finish its mouse release first.
                                QTimer::singleShot(0, this, [this, epoch] {
                                    if (!annotationMode || annotationEpoch != epoch || lifecycleHidden ||
                                        services.suspended()) {
                                        return;
                                    }
                                    finishAnnotations();
                                });
                            }});
    }
    menuActions.append({"rrAnnotationDismiss", ui::ControlIcon::Close, strings::get("annotations.dismiss"), [this] {
                            ++annotationEpoch;
                            annotationPopover->hide();
                            paintAnnotationDraft();
                        }});
    const int paletteHeight = colorAnnotations() && colorChanged ? ui::HighlightPalette::compactHeight() : 0;
    const int rowHeight = qBound(
        ui::px(56), (height() - 2 * int(m::notice::sideClearance) - paletteHeight) / qMax(1, int(menuActions.size())),
        int(m::links::actionHeight));
    for (const auto &action : menuActions) {
        auto *button = ui::makeCaptionButton(card, rows, action.name.toLatin1().constData(), action.label, action.run);
        button->setRule(rows->count() > 1 ? ui::Edges(Qt::TopEdge) : ui::Edges{});
        button->setIconVisible(true);
        button->setChevron(false);
        ui::buttonIcon(button, action.icon);
        ui::readerStyle(button,
                        QString("QPushButton{min-height:%1px;max-height:%1px;padding:0;border:none;}").arg(rowHeight));
        button->ensurePolished();
        button->show();
        const int rowWidth = m::chrome::inset + m::chrome::cellIcon + ui::px(20) +
                             qCeil(qt::textWidth(QFontMetricsF(button->font()), action.label)) + m::chrome::actionRight;
        desiredWidth = qMax(desiredWidth, rowWidth + 2 * int(m::control::border));
    }
    rows->activate();
    card->fitToWidth(qMin(desiredWidth, width() - 2 * int(m::notice::sideClearance)));
    annotationPopover->anchorTo(anchor);
    for (auto *widget : annotationPopover->findChildren<QWidget *>()) {
        widget->setPalette(palette());
        platform::textRefresh(widget);
    }
    annotationPopover->show();
    annotationPopover->raise();
    if (auto *palette =
            dynamic_cast<ui::HighlightPalette *>(annotationPopover->findChild<QWidget *>("rrAnnotationPalette"))) {
        palette->syncRepairBounds();
    }
}

void Reader::syncAnnotationHint() {
    syncMarkupRequest();
    const char *key = annotationStart.isEmpty() ? strings::key("annotations.tap_start")
                      : annotationEnd.isEmpty() ? strings::key("annotations.tap_end")
                      : annotationAdjusting     ? strings::key("annotations.tap_boundary")
                                                : strings::key("annotations.ready");
    annotationHint->setText(strings::get(key));
}

void Reader::clearAnnotationDraft() {
    ++annotationEpoch;
    annotationStart.clear();
    annotationEnd.clear();
    annotationEditing.clear();
    annotationColor = annotationSelectedColor;
    annotationStartWord = {};
    annotationEndWord = {};
    annotationStartPolicy = annotationEndPolicy = false;
    annotationChosenStart.clear();
    annotationChosenEnd.clear();
    annotationAdjusting = false;
    annotationDirty = false;
    annotationShowActionsPending = false;
    draftHighlightRects.clear();
    annotationActionAnchor = {};
    page->annotate({}, savedHighlightRects);
    annotationPopover->hide();
    syncAnnotationHint();
    annotationFrame.clear();
    syncAnnotations();
}

void Reader::setAnnotationBoundary(AnnotationToken token, bool start) {
    annotationDirty = true;
    annotationPopover->hide();
    if (start) {
        annotationStart = token.start;
        annotationStartWord = token;
        annotationStartPolicy = !token.punctuation;
    } else {
        annotationEnd = token.end;
        annotationEndWord = token;
        annotationEndPolicy = !token.punctuation;
    }
    if (annotationEnd.isEmpty()) {
        annotationAdjusting = false;
        syncAnnotationHint();
        updateAnnotationRects();
    } else {
        prepareAnnotation();
    }
}

void Reader::annotationTap(QPoint point) {
    if (!annotationMode || annotationSaving || model->busy() || lifecycleHidden || services.suspended()) {
        return;
    }
    annotationPopover->hide();
    paintAnnotationDraft();
    const auto epoch = ++annotationEpoch;
    if (!annotationStart.isEmpty() && draftHighlightRects.isEmpty()) {
        updateAnnotationRects();
    }
    if (!annotationEnd.isEmpty() && !annotationAdjusting && page->annotationContains(point)) {
        QRect nearest;
        int distance = std::numeric_limits<int>::max();
        for (const auto &rect : draftHighlightRects) {
            const int dx = qMax(rect.left() - point.x(), qMax(0, point.x() - rect.right()));
            const int dy = qMax(rect.top() - point.y(), qMax(0, point.y() - rect.bottom()));
            if (dx + dy < distance) {
                distance = dx + dy;
                nearest = rect;
            }
        }
        annotationActionAnchor = nearest.translated(0, page->topHeight());
        showAnnotationActions();
        return;
    }
    syncAnnotationHint();
    QPointer<Reader> guard(this);
    model->annotationTokenAt(point, [guard, epoch, point](AnnotationToken token) {
        if (!guard || !guard->annotationMode || guard->annotationEpoch != epoch || guard->lifecycleHidden ||
            guard->services.suspended()) {
            return;
        }
        auto *reader = guard.data();
        if (token.text.isEmpty()) {
            reader->annotationActionsFor({}, QRect(point + QPoint(0, reader->page->topHeight()), QSize(1, 1)));
            return;
        }
        QRect bounds;
        for (const auto &rect : token.rectangles) {
            bounds = bounds.united(rect);
        }
        bounds.translate(0, reader->page->topHeight());
        reader->annotationActionAnchor = bounds;
        QVector<QPair<QString, QString>> ranges;
        QVector<QString> ids;
        const auto &items = reader->highlights->highlights();
        for (int i = 0; i < items.size(); ++i) {
            if (items[i].id != reader->annotationEditing && reader->highlights->page(i) <= reader->model->page() &&
                reader->highlights->endPage(i) >= reader->model->page() && reader->highlights->page(i) >= 0) {
                ids.append(items[i].id);
                ranges.append(qMakePair(items[i].start, items[i].end));
            }
        }
        reader->model->intersectingRanges(
            token.start, token.end, ranges, [guard, epoch, token, bounds, ids](QVector<int> hits) {
                if (!guard || !guard->annotationMode || guard->annotationEpoch != epoch || guard->lifecycleHidden ||
                    guard->services.suspended()) {
                    return;
                }
                auto *reader = guard.data();
                QVector<QString> matches;
                for (const int index : hits) {
                    if (index >= 0 && index < ids.size()) {
                        matches.append(ids[index]);
                    }
                }
                const bool ready = !reader->annotationEnd.isEmpty() && !reader->annotationAdjusting;
                if (!matches.isEmpty() && (reader->annotationStart.isEmpty() || ready)) {
                    if (matches.size() == 1) {
                        reader->showSavedAnnotation(matches.first(), bounds);
                    } else {
                        reader->showAnnotationMatches(matches, bounds);
                    }
                    return;
                }
                if (ready) {
                    reader->annotationActionsFor({}, bounds);
                    return;
                }
                if (reader->annotationStart.isEmpty()) {
                    reader->setAnnotationBoundary(token, true);
                    return;
                }
                const bool sameStart = token.start == reader->annotationStartWord.start &&
                                       (reader->annotationStartWord.end == reader->annotationStartWord.start ||
                                        token.end == reader->annotationStartWord.end);
                QVector<AnnotationAction> actions;
                if (!sameStart) {
                    actions.append({"rrAnnotationEnd", ui::ControlIcon::SelectionEnd,
                                    strings::get("annotations.end_here"), [reader, token] {
                                        reader->setAnnotationBoundary(token, false);
                                    }});
                }
                if (sameStart && !token.punctuation) {
                    actions.append({"rrAnnotationHighlightWord", ui::ControlIcon::Annotate,
                                    strings::get("annotations.highlight_word"), [reader, token] {
                                        reader->annotationStart = token.start;
                                        reader->annotationEnd = token.end;
                                        reader->annotationChosenStart.clear();
                                        reader->annotationChosenEnd.clear();
                                        reader->annotationStartWord = token;
                                        reader->annotationEndWord = token;
                                        reader->annotationStartPolicy = !token.punctuation;
                                        reader->annotationEndPolicy = !token.punctuation;
                                        reader->annotationDirty = true;
                                        reader->annotationAdjusting = false;
                                        reader->prepareAnnotation();
                                    }});
                }
                reader->annotationActionsFor(actions, bounds, true, reader->annotationColor, [reader](QString color) {
                    reader->annotationColor = color;
                    reader->annotationColorChosen = true;
                    reader->annotationSelectedColor = color;
                });
                reader->paintAnnotationDraft();
                if (!sameStart) {
                    QPointer<QWidget> endAction(reader->annotationPopover->findChild<QWidget *>("rrAnnotationEnd"));
                    reader->model->annotationRange(
                        reader->annotationStartWord.start, reader->annotationStartWord.end, token.start, token.end,
                        [guard, epoch, endAction](TextRange range) {
                            if (!guard || !endAction || !guard->annotationMode || guard->annotationEpoch != epoch ||
                                guard->lifecycleHidden || guard->services.suspended() ||
                                !guard->annotationPopover->isVisible() || range.text.isEmpty()) {
                                return;
                            }
                            guard->model->annotationGeometry(
                                range.start, range.end, [guard, epoch, endAction](AnnotationGeometry geometry) {
                                    if (!guard || !endAction || !guard->annotationMode ||
                                        guard->annotationEpoch != epoch || guard->lifecycleHidden ||
                                        guard->services.suspended() || !guard->annotationPopover->isVisible()) {
                                        return;
                                    }
                                    guard->page->annotate(geometry.rectangles, guard->savedHighlightRects, {},
                                                          geometry.vertical);
                                });
                        },
                        !token.punctuation, reader->annotationStartPolicy);
                }
            });
    });
}

void Reader::prepareAnnotation(bool leave, std::function<void()> after) {
    annotationPopover->hide();
    annotationSaving = true;
    ++annotationEpoch;
    const auto transaction = ++annotationSaveRevision;
    QPointer<Reader> guard(this);
    model->annotationRange(
        annotationStartWord.start, annotationStartWord.end, annotationEndWord.start, annotationEndWord.end,
        [guard, transaction, leave, after](TextRange range) {
            if (!guard) {
                return;
            }
            guard->annotationSaving = false;
            if (!guard->annotationMode || guard->annotationSaveRevision != transaction) {
                return;
            }
            if (range.text.isEmpty()) {
                guard->annotationHint->setText(strings::get("annotations.save_failed"));
                guard->annotationShowActionsPending = true;
                guard->annotationFrame.clear();
                guard->syncAnnotations();
                return;
            }
            guard->annotationStart = range.start;
            guard->annotationEnd = range.end;
            if (range.chosenStart != guard->annotationStartWord.start) {
                std::swap(guard->annotationStartWord, guard->annotationEndWord);
                std::swap(guard->annotationStartPolicy, guard->annotationEndPolicy);
            }
            guard->annotationChosenStart = range.chosenStart;
            guard->annotationStartPolicy = range.sentenceStartPolicy;
            guard->annotationChosenEnd = range.chosenEnd;
            guard->annotationEndPolicy = range.sentenceEndPolicy;
            guard->annotationAdjusting = false;
            guard->syncAnnotationHint();
            guard->saveAnnotation(leave, after);
        },
        annotationEndPolicy, annotationStartPolicy);
}

void Reader::showAnnotationActions() {
    QRect bounds = annotationActionAnchor;
    if (bounds.isEmpty() && !draftHighlightRects.isEmpty()) {
        bounds = draftHighlightRects.last().translated(0, page->topHeight());
    }
    if (bounds.isEmpty()) {
        bounds = annotationHeader->geometry();
    }
    annotationActionsFor({{"rrAnnotationSave", ui::ControlIcon::SaveHighlight, strings::get("annotations.retry_save"),
                           [this] {
                               prepareAnnotation();
                           }}},
                         bounds);
}

}

namespace kobalt::reader {
void Reader::saveAnnotation(bool leave, std::function<void()> after) {
    if (annotationStart.isEmpty() || annotationEnd.isEmpty()) {
        return;
    }
    annotationPopover->hide();
    annotationSaving = true;
    ++annotationEpoch;
    const auto transaction = annotationSaveRevision;
    QPointer<Reader> guard(this);
    highlights->save(
        annotationStart, annotationEnd, annotationEditing,
        [guard, transaction, leave, after](bool saved) {
            if (!guard) {
                return;
            }
            guard->annotationSaving = false;
            if (!guard->annotationMode || guard->annotationSaveRevision != transaction) {
                guard->annotationShowActionsPending = false;
                guard->annotationFrame.clear();
                guard->syncAnnotations();
                return;
            }
            if (!saved) {
                guard->annotationHint->setText(strings::get("annotations.save_failed"));
                guard->annotationShowActionsPending = true;
                guard->annotationFrame.clear();
                guard->syncAnnotations();
                return;
            }
            guard->annotationDirty = false;
            guard->annotationShowActionsPending = false;
            guard->annotationStart.clear();
            guard->annotationEnd.clear();
            guard->annotationEditing.clear();
            guard->annotationColor = guard->annotationSelectedColor;
            guard->draftHighlightRects.clear();
            guard->annotationAdjusting = false;
            guard->annotationActionAnchor = {};
            guard->syncAnnotationHint();
            guard->annotationFrame.clear();
            if (after && guard->isVisible() && !guard->lifecycleHidden && !guard->services.suspended()) {
                after();
            } else if (leave) {
                guard->leaveAnnotations();
            } else {
                guard->syncAnnotations();
            }
        },
        annotationChosenEnd, annotationEndPolicy, annotationChosenStart, annotationStartPolicy,
        colorAnnotations() && (annotationEditing.isEmpty() || annotationColorChosen) ? annotationColor : QString());
}

void Reader::updateAnnotationRects(bool showActions) {
    const auto epoch = annotationEpoch;
    QPointer<Reader> guard(this);
    if (annotationStart.isEmpty()) {
        draftHighlightRects.clear();
        page->annotate({}, savedHighlightRects);
        return;
    }
    const QString end = annotationEnd.isEmpty() ? annotationStartWord.end : annotationEnd;
    model->annotationGeometry(annotationStart, end, [guard, epoch, showActions](AnnotationGeometry geometry) {
        if (!guard || !guard->annotationMode || guard->annotationEpoch != epoch || guard->lifecycleHidden ||
            guard->services.suspended()) {
            return;
        }
        guard->draftHighlightRects = geometry.rectangles;
        guard->draftHighlightVertical = geometry.vertical;
        guard->paintAnnotationDraft();
        if (showActions) {
            guard->showAnnotationActions();
        }
    });
}

void Reader::layoutAnnotationHeader() {
    if (!annotationHeader || !annotationHint) {
        return;
    }
    const int textHeight = qMax(ui::px(72) - int(m::chrome::heavyRule),
                                annotationHint->preferredHeight(qMax(1, width() - int(m::chrome::cellWidth))));
    const int barHeight = textHeight + m::chrome::heavyRule;
    annotationHeader->setGeometry(0, 0, width(), barHeight);
    annotationHeader->findChild<QWidget *>("rrAnnotationClose")->setFixedSize(m::chrome::cellWidth, textHeight);
}

void Reader::syncMarkupRequest() {
    ReaderMarkupRequest request;
    for (const auto &item : highlights->highlights()) {
        if (item.id != annotationEditing) {
            request.saved.append(
                {item.start, item.end, colorAnnotations() ? highlightDisplayColor(item.color) : QString("gray")});
        }
    }
    if (annotationMode && !annotationStart.isEmpty()) {
        request.draftStart = annotationStart;
        request.draftEnd = annotationEnd.isEmpty() ? annotationStartWord.end : annotationEnd;
    }
    annotationRequest = std::move(request);
    model->setMarkup(annotationRequest);
    if (auto *preview = appearanceEdit.preview()) {
        preview->setMarkup(annotationRequest);
    }
}

void Reader::preparePresentation(std::function<void(bool)> finished) {
    syncMarkupRequest();
    if (annotationRequest.saved.isEmpty()) {
        finished(true);
        return;
    }
    annotationPresentationReady = std::move(finished);
    refresh();
}

void Reader::finishAnnotationPresentation() {
    if (!annotationPresentationReady || annotationPresentationCapture) {
        return;
    }
    if (closing || lifecycleHidden || !model->error().isEmpty()) {
        auto finished = std::exchange(annotationPresentationReady, {});
        finished(false);
        return;
    }
    if (model->busy() || model->image().isNull()) {
        return;
    }
    syncMarkupRequest();
    const auto &markup = model->markup();
    if (markup.imageKey == model->image().cacheKey() && markup.request.saved == annotationRequest.saved) {
        syncAnnotations();
        auto finished = std::exchange(annotationPresentationReady, {});
        finished(true);
        return;
    }
    annotationPresentationCapture = true;
    QPointer<Reader> guard(this);
    model->captureMarkup([guard](bool ready) {
        if (!guard) {
            return;
        }
        guard->annotationPresentationCapture = false;
        if (!ready) {
            if (guard->model->busy()) {
                return;
            }
            auto finished = std::exchange(guard->annotationPresentationReady, {});
            if (finished) {
                finished(false);
            }
            return;
        }
        guard->refresh();
        if (guard) {
            guard->finishAnnotationPresentation();
        }
    });
}

void Reader::syncAnnotations() {
    if (!annotationHeader || model->busy()) {
        return;
    }
    layoutAnnotationHeader();
    annotationHeader->setVisible(annotationMode && !minimalAnnotationMode());
    if (annotationMode) {
        if (annotationHeader->isVisible()) {
            annotationHeader->raise();
        }
        if (annotationPopover->isVisible()) {
            annotationPopover->raise();
        }
    }
    const QString frame = layoutKey() + ':' + model->state().value("position").toString();
    if (annotationViewportFrame != frame) {
        ++annotationEpoch;
        annotationViewportFrame = frame;
        annotationActionAnchor = {};
        annotationPopover->hide();
    }
    syncMarkupRequest();
    const QString tintFrame = frame + (colorAnnotations() ? ":color" : ":mono");
    const auto imageKey = page->image.cacheKey();
    if (annotationImageKey != imageKey) {
        ++annotationEpoch;
        annotationActionAnchor = {};
        annotationPopover->hide();
    }
    const ReaderSession *displayed = model;
    if (const auto *preview = appearanceEdit.preview()) {
        if (preview->image().cacheKey() == imageKey) {
            displayed = preview;
        }
    }
    const auto &markup = displayed->markup();
    const bool canShowActions = annotationMode && isVisible() && !lifecycleHidden && !services.suspended();
    const bool showActions = canShowActions && annotationShowActionsPending;
    const bool unchangedMarkup = annotationFrame != tintFrame && markup.request == annotationRequest;
    if ((annotationImageKey != imageKey || annotationPresentationReady || unchangedMarkup) &&
        markup.imageKey == imageKey && markup.request.saved == annotationRequest.saved) {
        ++savedHighlightEpoch;
        annotationImageKey = imageKey;
        annotationFrame = tintFrame;
        savedHighlightRects.clear();
        for (const auto &group : markup.saved) {
            savedHighlightRects += group.first;
        }
        const bool matchingDraft = annotationMode && markup.request.draftStart == annotationRequest.draftStart &&
                                   markup.request.draftEnd == annotationRequest.draftEnd;
        draftHighlightRects = matchingDraft ? markup.draft.rectangles : QVector<QRect>{};
        draftHighlightVertical = markup.draft.vertical;
        page->installAnnotationFrame(draftHighlightRects, markup.saved, draftHighlightVertical);
        highlights->sync();
        if (showActions) {
            annotationShowActionsPending = false;
            showAnnotationActions();
        }
        return;
    }
    annotationImageKey = imageKey;
    if (annotationPresentationReady) {
        return;
    }
    if (!highlights->sync()) {
        return;
    }
    if (annotationFrame == tintFrame) {
        if (showActions) {
            annotationShowActionsPending = false;
            updateAnnotationRects(true);
        }
        return;
    }
    annotationFrame = tintFrame;
    if (showActions) {
        annotationShowActionsPending = false;
    }
    updateAnnotationRects(showActions);
    savedHighlightRects.clear();
    draftHighlightRects.clear();
    page->annotate({}, {});
    const auto epoch = ++savedHighlightEpoch;
    QVector<QPair<QString, QString>> ranges;
    QVector<QString> colors;
    const auto &items = highlights->highlights();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].id != annotationEditing && highlights->page(i) <= model->page() &&
            highlights->endPage(i) >= model->page() && highlights->page(i) >= 0) {
            ranges.append(qMakePair(items[i].start, items[i].end));
            colors.append(colorAnnotations() ? highlightDisplayColor(items[i].color) : QString("gray"));
        }
    }
    if (ranges.isEmpty()) {
        return;
    }
    QPointer<Reader> guard(this);
    model->rangeRectGroups(ranges, [guard, epoch, colors](QVector<QVector<QRect>> groups) {
        if (!guard || guard->savedHighlightEpoch != epoch) {
            return;
        }
        QVector<QRect> rects;
        QVector<QPair<QVector<QRect>, QString>> tinted;
        for (int i = 0; i < groups.size(); ++i) {
            rects += groups[i];
            tinted.append(qMakePair(groups[i], colors.value(i, "gray")));
        }
        guard->savedHighlightRects = rects;
        guard->paintAnnotationDraft();
        guard->page->setAnnotationColors(tinted);
    });
}

QVector<BookmarkRow> Reader::annotationRows() const {
    QVector<BookmarkRow> rows;
    const auto &items = highlights->highlights();
    for (int i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        BookmarkRow row{item.id, item.chapter, item.text, item.created, highlights->page(i), false};
        row.endPage = highlights->endPage(i);
        rows.append(row);
    }
    return rows;
}

void Reader::editAnnotation(const QString &id, bool navigate) {
    if (annotationDirty && !annotationStart.isEmpty() && annotationEditing != id) {
        QRect anchor = annotationActionAnchor.isEmpty() ? annotationHeader->geometry() : annotationActionAnchor;
        annotationActionsFor(
            {{"rrAnnotationSwitchSave", ui::ControlIcon::SaveHighlight, strings::get("annotations.save_then_edit"),
              [this, id, navigate] {
                  prepareAnnotation(false, [this, id, navigate] {
                      editAnnotation(id, navigate);
                  });
              }},
             {"rrAnnotationSwitchDiscard", ui::ControlIcon::ClearSelection,
              strings::get("annotations.discard_then_edit"),
              [this, id, navigate] {
                  clearAnnotationDraft();
                  editAnnotation(id, navigate);
              }}},
            anchor);
        annotationPopover->findChild<QPushButton *>("rrAnnotationSwitchSave")->setEnabled(!annotationEnd.isEmpty());
        return;
    }
    for (const auto &item : highlights->highlights()) {
        if (item.id != id) {
            continue;
        }
        bookmarkList->hide();
        enterAnnotations();
        annotationEditing = id;
        annotationColorChosen = false;
        annotationColor = item.color;
        annotationStart = item.start;
        annotationEnd = item.end;
        annotationStartWord = {};
        annotationEndWord = {};
        annotationStartWord.start = item.sentenceStartPolicy ? item.chosenStart : item.start;
        annotationStartWord.end = annotationStartWord.start;
        annotationChosenStart = annotationStartWord.start;
        annotationEndWord.start = item.sentenceEndPolicy ? item.chosenEnd : item.end;
        annotationEndWord.end = annotationEndWord.start;
        annotationChosenEnd = annotationEndWord.start;
        annotationStartPolicy = item.sentenceStartPolicy;
        annotationEndPolicy = item.sentenceEndPolicy;
        annotationAdjusting = true;
        annotationDirty = false;
        annotationShowActionsPending = false;
        annotationPopover->hide();
        if (navigate) {
            model->goTo(item.start);
        }
        syncAnnotationHint();
        annotationFrame.clear();
        syncAnnotations();
        return;
    }
}

void Reader::showSavedAnnotation(const QString &id, QRect anchor, bool navigate) {
    annotationActionsFor(
        {{"rrAnnotationEdit", ui::ControlIcon::EditHighlight, strings::get("annotations.edit"),
          [this, id, navigate, anchor] {
              annotationPopover->hide();
              annotationActionAnchor = anchor;
              editAnnotation(id, navigate);
          }},
         {"rrAnnotationRemove", ui::ControlIcon::RemoveHighlight, strings::get("annotations.remove"),
          [this, id] {
              annotationPopover->hide();
              removeAnnotation(id);
          }}},
        anchor, true,
        [&] {
            for (const auto &item : highlights->highlights()) {
                if (item.id == id) {
                    return item.color;
                }
            }
            return QString("gray");
        }(),
        [this, id, anchor](QString color) {
            const bool saved = highlights->recolor(id, color);
            if (saved) {
                annotationSelectedColor = color;
                annotationFrame.clear();
                syncAnnotations();
            }
            // Keep the pressed swatch alive until the native mouse release has returned.
            auto *palette =
                dynamic_cast<ui::HighlightPalette *>(annotationPopover->findChild<QWidget *>("rrAnnotationPalette"));
            if (palette) {
                for (const auto &item : highlights->highlights()) {
                    if (item.id == id) {
                        palette->setSelectedColor(item.color);
                        break;
                    }
                }
                palette->setError(saved ? QString() : strings::get("annotations.color_failed"));
                auto *card = annotationPopover->card();
                card->rows()->activate();
                card->fitToWidth(card->width());
                annotationPopover->anchorTo(anchor);
                palette->syncRepairBounds();
            }
        });
}

void Reader::showAnnotationMatches(QVector<QString> ids, QRect anchor, int listPage) {
    const int reservedRows = annotationMode ? (annotationStart.isEmpty() ? 4 : 5) : 3;
    const int pageSize =
        qBound(1, (height() - 2 * int(m::notice::sideClearance)) / int(m::links::actionHeight) - reservedRows, 3);
    const int pages = qMax(1, (ids.size() + pageSize - 1) / pageSize);
    listPage = qBound(0, listPage, pages - 1);
    QVector<AnnotationAction> actions;
    for (int i = listPage * pageSize; i < qMin(ids.size(), (listPage + 1) * pageSize); ++i) {
        for (const auto &item : highlights->highlights()) {
            if (item.id != ids[i]) {
                continue;
            }
            QString excerpt = item.text.simplified();
            if (excerpt.size() > 36) {
                excerpt = excerpt.left(36) + QString::fromUtf8("…");
            }
            excerpt = QString::number(i + 1) + ". " + excerpt;
            const QString name = QString("rrAnnotationMatch%1").arg(i);
            const QString id = item.id;
            actions.append({name, ui::ControlIcon::Annotate, excerpt, [this, id, anchor] {
                                showSavedAnnotation(id, anchor);
                            }});
        }
    }
    if (pages > 1) {
        actions.append({"rrAnnotationMatchPrevious", ui::ControlIcon::Previous,
                        strings::get("annotations.previous_matches"), [this, ids, anchor, listPage, pages] {
                            showAnnotationMatches(ids, anchor, (listPage + pages - 1) % pages);
                        }});
        actions.append({"rrAnnotationMatchNext", ui::ControlIcon::Next, strings::get("annotations.next_matches"),
                        [this, ids, anchor, listPage, pages] {
                            showAnnotationMatches(ids, anchor, (listPage + 1) % pages);
                        }});
    }
    annotationActionsFor(actions, anchor);
}

void Reader::removeAnnotation(const QString &id) {
    if (removeDialog) {
        return;
    }
    auto *choice = new ui::ChoiceModal(
        this, {strings::get("annotations.remove_detail"), strings::get("annotations.remove_prompt"), {}},
        {strings::get("common.cancel"), "rrKeepAnnotation"},
        {strings::get("annotations.remove"), "rrConfirmRemoveAnnotation"});
    removeDialog = choice;
    choice->setObjectName("rrRemoveAnnotation");
    const auto finish = [this, id, choice](bool remove) {
        choice->hide();
        choice->deleteLater();
        removeDialog.clear();
        if (remove) {
            highlights->remove(id);
            annotationFrame.clear();
            if (bookmarkList->isVisible()) {
                bookmarkList->showAnnotations(annotationRows());
            }
            syncAnnotations();
        }
    };
    connect(choice->secondaryButton(), &QPushButton::clicked, this, [finish] {
        finish(false);
    });
    connect(choice->primaryButton(), &QPushButton::clicked, this, [finish] {
        finish(true);
    });
    choice->card()->fitToWidth(qMin<int>(m::notice::cardWidth, width() - 2 * m::notice::sideClearance));
    choice->centerCard();
    choice->show();
    choice->raise();
}
}
