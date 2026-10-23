#pragma once
#include "sliders.h"
#include <QImage>
#include <QPair>
#include <QTimer>

class QLabel;
class QPushButton;
namespace kobalt::ui {
class Panel;
class CaptionButton;
class PageThumbnail;
// Receives page images and chapter positions from its owner. Scrubbing never
// changes the book; only accepted reports a destination to the reader.
class PageScrubber : public QWidget {
public:
    explicit PageScrubber(QWidget *parent);
    SettingSlider *slider() const {
        return position;
    }
    void setBook(int page, int pages, const QVector<QPair<int, QString>> &chapters, QSize pageSize);
    void setChapterJumps(bool enabled);
    void setPreview(int page, QImage image);
    void cancel();
    void setLinksAction(const QString &label);
    void setReturnAction(const QString &label, const QString &note);
    void setBarVisible(bool visible);
    bool hasActions() const {
        return actionsHeight() > 0;
    }
    bool selecting() const {
        return candidate >= 0;
    }
    int selectedPage() const {
        return candidate;
    }
    bool previewReady() const {
        return !preview.isNull();
    }
    bool previewsReady() const;
    QRect previewBounds() const;
    QRect barBounds() const;
    QRect panelBounds() const;
    std::function<void(int, QSize)> previewRequested;
    std::function<void(int)> accepted;
    std::function<void()> cancelled;
    std::function<void()> linksRequested, returnRequested, stayRequested;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void hideEvent(QHideEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    bool event(QEvent *) override;

private:
    SettingSlider *position;
    QPushButton *previous, *next, *zoomOut, *zoomIn, *overviewToggle;
    RangeOverview *overview;
    QLabel *rangeLabel, *overviewLabel, *rangeStart, *rangeEnd, *overviewStart, *overviewEnd;
    QLabel *chapterLabel;
    Panel *barRule, *linksRule, *returnRule, *captionBackground, *overviewRule;
    QWidget *actions;
    CaptionButton *linksAction, *returnAction;
    QPushButton *stay;
    QTimer renderTimer;
    QVector<QPair<int, QString>> chapters;
    int current = 0, candidate = -1, totalPages = 1;
    int zoomLevel = 0, rangeFirst = 0, rangeLast = 0;
    QImage preview;
    PageThumbnail *preceding, *following;
    int pendingPreview = -1;
    bool precedingRequested = false, followingRequested = false;
    QSize pageSize{3, 4};
    QRect linksFrame, returnFrame;
    bool pressedPreview = false, chapterJumps = false, barVisible = true, relocating = false, overviewExpanded = false;
    void choose(int page);
    void step(int direction);
    void updateButtons();
    int maximumZoomLevel() const;
    int initialZoomLevel() const;
    int zoomSpan(int level) const;
    bool zoomAvailable() const;
    void zoom(int direction);
    void centerRange(int page);
    void syncRange();
    void pan(int first);
    void requestPreview();
    void requestAdjacentPreview();
    void clearPreview();
    void updateMask();
    void updateCaption();
    void release(QPoint point);
    QRect imageBounds() const;
    int actionsHeight() const;
    void arrange();
};
}
