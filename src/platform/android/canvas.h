#pragma once
#include <QAbstractButton>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QMouseEvent>
#include <QPointer>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QResizeEvent>
#include <functional>

namespace kobalt::android {
// Qt caches each damaged rectangle, but its default proxy paints their bounding
// rectangle. Pass the cache's clip region to QWidget to keep the work local.
class RegionProxy final : public QGraphicsProxyWidget {
public:
    explicit RegionProxy(bool nativePixels = false) : nativePixels(nativePixels) {
        setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override {
        if (!widget() || !widget()->isVisible())
            return;
        // The cache clears a pixel-aligned margin outside exposedRect. Repaint
        // its entire clip, or that margin becomes a blank strip on hover.
        const QRegion region =
            (painter->hasClipping() ? painter->clipRegion() : QRegion(option->exposedRect.toAlignedRect())) &
            QRegion(rect().toAlignedRect());
        if (region.isEmpty())
            return;
        if (!nativePixels) {
            widget()->render(painter, region.boundingRect().topLeft(), region);
            return;
        }
        // Render the complete screen in device coordinates before scaling.
        // Sampling its text, images and controls once avoids uneven strokes
        // from rendering individual widgets at a fractional preview scale.
        QRegion damage = region;
        if (pixels.size() != widget()->size()) {
            pixels = QImage(widget()->size(), QImage::Format_ARGB32_Premultiplied);
            pixels.fill(Qt::transparent);
            damage = widget()->rect();
        }
        {
            QPainter source(&pixels);
            widget()->render(&source, damage.boundingRect().topLeft(), damage);
        }
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(QPointF(), pixels);
        painter->restore();
    }

private:
    bool nativePixels;
    QImage pixels;
};
// Keep the existing UI in physical pixels. Qt maps its damage rectangles to
// the Android surface. Deliver touch as ordinary mouse input because the
// embedded QWidget does not receive Android's touch events through the proxy.
class WidgetCanvas : public QGraphicsView {
public:
    explicit WidgetCanvas(QWidget *parent) : QGraphicsView(parent) {
        viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        // Use regional updates for separate scene items and popup widgets.
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        setScene(new QGraphicsScene(this));
    }
    void setContent(QWidget *widget, bool nativePixels = false) {
        content = widget;
        auto *proxy = new RegionProxy(nativePixels);
        scene()->addItem(proxy);
        proxy->setWidget(widget);
    }
    QSize pixelSize() const {
        const auto dpr = devicePixelRatioF();
        return {qRound(viewport()->width() * dpr), qRound(viewport()->height() * dpr)};
    }
    void fitContent() {
        const auto pixels = pixelSize();
        if (pixels.isEmpty())
            return;
        if (content)
            content->resize(pixels);
        const auto dpr = devicePixelRatioF();
        setTransform(QTransform::fromScale(1.0 / dpr, 1.0 / dpr));
        setSceneRect(QRectF(QPointF(), pixels));
    }
    QPointer<QWidget> content;
    std::function<void()> cancelGesture;
    std::function<void()> viewportResized;

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        if (viewportResized)
            viewportResized();
    }
    bool event(QEvent *event) override {
        const auto type = event->type();
        const bool handled = QGraphicsView::event(event);
        bool scaleChanged = type == QEvent::ScreenChangeInternal;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        scaleChanged = scaleChanged || type == QEvent::DevicePixelRatioChange;
#endif
        if (scaleChanged && viewportResized)
            viewportResized();
        return handled;
    }
    bool viewportEvent(QEvent *event) override {
        const auto type = event->type();
        if (type == QEvent::TouchBegin || type == QEvent::TouchUpdate || type == QEvent::TouchEnd ||
            type == QEvent::TouchCancel) {
            const auto *touch = static_cast<QTouchEvent *>(event);
            // Trackpads already send pointer events. Their finger positions
            // are not screen coordinates and must not become a second mouse gesture.
            if (touch->pointingDevice() && touch->pointingDevice()->type() == QInputDevice::DeviceType::TouchPad) {
                event->accept();
                return true;
            }
        }
        if (type == QEvent::TouchCancel) {
            cancel();
            event->accept();
            return true;
        }
        if (type != QEvent::TouchBegin && type != QEvent::TouchUpdate && type != QEvent::TouchEnd)
            return QGraphicsView::viewportEvent(event);
        const auto *touch = static_cast<QTouchEvent *>(event);
        if (!content || touch->points().size() != 1) {
            cancel();
            event->accept();
            return true;
        }
        const auto position = mapToScene(touch->points().first().position().toPoint()).toPoint();
        if (type == QEvent::TouchBegin) {
            target = content->childAt(position);
            if (!target)
                target = content;
        }
        if (target) {
            const bool release = type == QEvent::TouchEnd;
            const auto mouseType = type == QEvent::TouchBegin ? QEvent::MouseButtonPress
                                   : release                  ? QEvent::MouseButtonRelease
                                                              : QEvent::MouseMove;
            const QPoint local = target->mapFrom(content, position);
            QMouseEvent mouse(mouseType, local, target->mapToGlobal(local),
                              type == QEvent::TouchUpdate ? Qt::NoButton : Qt::LeftButton,
                              release ? Qt::NoButton : Qt::LeftButton, touch->modifiers());
            QApplication::sendEvent(target, &mouse);
            if (release)
                target.clear();
        }
        event->accept();
        return true;
    }

private:
    QPointer<QWidget> target;
    void cancel() {
        // Cancellation handlers can delete the target or start another gesture.
        QPointer<QWidget> cancelled = target;
        target.clear();
        if (cancelled) {
            QTouchEvent event(QEvent::TouchCancel);
            QApplication::sendEvent(cancelled, &event);
        }
        if (auto *button = qobject_cast<QAbstractButton *>(cancelled.data()))
            button->setDown(false);
        if (content && cancelGesture)
            cancelGesture();
    }
};

}
