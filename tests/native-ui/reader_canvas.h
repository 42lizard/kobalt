#pragma once

#include <QImage>
#include <QPointer>
#include <QScrollArea>

namespace kobalt::test {

// Keep QWidget coordinates unchanged. The reader compares global popup origins
// with widget sizes; a scaled QGraphicsProxyWidget scales only the origins in
// those comparisons. Scroll the native-size surface instead, and capture it
// independently of the host window's size and Retina device pixel ratio.
class ReaderCanvas : public QScrollArea {
public:
    ReaderCanvas(QWidget *surface, QSize nativeSize, QWidget *parent = nullptr)
        : QScrollArea(parent), surface_(surface), nativeSize_(nativeSize) {
        Q_ASSERT(surface);
        Q_ASSERT(nativeSize.isValid() && !nativeSize.isEmpty());
        setWidgetResizable(false);
        setFrameShape(QFrame::NoFrame);
        setAlignment(Qt::AlignLeft | Qt::AlignTop);
        surface->setWindowFlags(Qt::Widget);
        surface->setWindowModality(Qt::NonModal);
        surface->setFixedSize(nativeSize);
        const bool autoFill = surface->autoFillBackground();
        setWidget(surface);
        surface->setAutoFillBackground(autoFill);
    }

    QWidget *surface() const {
        return surface_;
    }
    QSize nativeSize() const {
        return nativeSize_;
    }

    QImage capture() const {
        if (!surface_)
            return {};
        QList<QPointer<QWidget>> existing;
        for (auto *widget : surface_->findChildren<QWidget *>())
            existing.append(widget);
        QImage image(nativeSize_, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(1);
        image.fill(Qt::transparent);
        surface_->render(&image);
        if (!surface_)
            return {};
        // Qt 6.5.2 QWidgetPrivate::prepareToRender calls
        // sendPendingMoveAndResizeEvents(true, true). It temporarily disables
        // a hidden panel while delivering resizeEvent, then clears only that
        // panel's flag. Children created by the resize inherit the disabled
        // flag and can remain unpainted after capture. Repair only those new
        // children whose disabled state is inherited from that temporary
        // flag. Preserve preexisting and explicitly disabled widgets.
        bool repaired = false;
        for (auto *widget : surface_->findChildren<QWidget *>()) {
            bool existed = false;
            for (const auto &prior : existing)
                if (prior && prior == widget) {
                    existed = true;
                    break;
                }
            if (existed || widget->updatesEnabled() || widget->testAttribute(Qt::WA_ForceUpdatesDisabled))
                continue;
            bool ancestorsEnabled = true;
            for (auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
                ancestorsEnabled &= parent->updatesEnabled();
            if (!ancestorsEnabled)
                continue;
            // Do not use setUpdatesEnabled: its recursive propagation could
            // enable an older disabled widget reparented beneath a new one.
            widget->setAttribute(Qt::WA_UpdatesDisabled, false);
            repaired = true;
        }
        if (repaired) {
            image.fill(Qt::transparent);
            surface_->render(&image);
        }
        return image;
    }

private:
    QPointer<QWidget> surface_;
    QSize nativeSize_;
};

}
