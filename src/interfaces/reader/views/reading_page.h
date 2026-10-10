#pragma once
#include "interfaces/reader/ui/style/metrics.h"
#include "ui/brightness_gesture.h"
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <functional>
#include <tuple>
class QPainter;

namespace kobalt {
class ReadingPage : public QWidget {
public:
    QImage image;
    int animationDirection = 0;
    QString progress, chapterTitle, chapterProgress, clockText;
    bool showHeader = true, showFooter = true, showClock = true, showProgress = true;
    qreal progressFraction = 0;
    QString headerSize = "small", footerSize = "small";
    int headerTextPixels = ui::metrics::reading::textPixels({}, false),
        footerTextPixels = ui::metrics::reading::textPixels({}, true);
    bool hideDeviceStatus = false;
    bool clockVisible() const {
        return showClock && !hideDeviceStatus;
    }
    void setPanelBelowHeader(bool visible);
    int topHeight() const;
    int bottomHeight() const;
    // The footer's text line, in page coordinates. Empty without a footer.
    QRect footerTextRect() const;
    // The ribbon that marks a bookmarked page, and the corner that toggles it.
    bool bookmarked = false;
    QRect ribbonRect() const;
    QRect bookmarkCorner() const;
    std::function<void(QPoint)> held;
    // Ends the current touch: no lookup, tap, swipe or light change follows.
    void cancelHold();
    // Stops only the lookup that a long hold would start. A tap or swipe still completes.
    void stopHold() {
        holdTimer.stop();
    }
    // A vertical drag along the left edge changes brightness and one along
    // the right edge changes warmth, when each edge is enabled. `lightStarted`
    // receives true for warmth and may refuse the drag.
    bool brightnessEdge = true, warmthEdge = false;
    std::function<bool(bool)> lightStarted;
    std::function<void(double)> lightChanged;
    std::function<void(bool)> lightFinished;
    void mark(const QVector<QRect> &rectangles);
    void revealLinks(const QVector<QRect> &rectangles);
    std::function<void(QPoint)> tapped;
    std::function<void(int)> swiped;
    ReadingPage(QWidget *parent, std::function<void()> cancelAnimation);
    void cleanRefresh(bool clean);
    void refreshContent(bool reading = false, bool now = false);
    // Set before refreshContent when the engine prepared the changed rectangle
    // from the image with this cache key to `image`. 0 means not prepared.
    QRect preparedDamage;
    qint64 preparedFrom = 0;

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    std::function<void()> cancelAnimation;
    // The last field is the interface text weight: a change draws the strips again.
    using HeaderState = std::tuple<QString, QString, QString, bool, bool, bool, int, int, qint64, int>;
    using FooterState = std::tuple<QString, bool, bool, qreal, int, int, qint64, int>;
    bool panelBelowHeader = false;
    HeaderState headerState;
    FooterState footerState;
    QImage renderedBody, headerPixels, footerPixels;
    QSize renderedSize;
    qint64 renderedPalette = 0;
    int renderedTop = -1, renderedBottom = -1;
    QRect renderedRibbon;
    QImage renderStrip(int height, const std::function<void(QPainter &)> &draw);
    void paintHeader(QPainter &painter) const;
    void paintFooter(QPainter &painter) const;
    int headerProgressWidth() const;
    QTimer holdTimer;
    QVector<QRect> selection, revealedLinks;
    QVector<QRect> revealedOutlines;
    bool tracking = false, moved = false, consumed = false;
    QPointF origin, latest;
    kobalt::ui::BrightnessGesture brightnessGesture;
    void updatePointer();
    void finishGesture();
};

}
