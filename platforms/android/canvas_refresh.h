#pragma once
#include "boox_frame_queue.h"
#include "boox_refresh.h"
#include "canvas.h"
#include <QTimer>

namespace kobalt::android {
// Each app canvas uses the same Surface protocol. Only reading pages add
// REGAL text regions; Library, opening and dialog controls use GU regions.
class CanvasRefresh final : public QObject {
public:
    CanvasRefresh(WidgetCanvas *canvas, std::function<bool()> available,
                  std::function<bool(const RefreshFrame &)> submit, bool cleanOnReveal = true)
        : QObject(canvas), canvas(canvas), available(std::move(available)), submit(std::move(submit)),
          cleanOnReveal(cleanOnReveal), cleanPending(cleanOnReveal) {
        timer.setSingleShot(true);
        // Leave room for Qt's queued backing-store flush before the next region.
        timer.setInterval(34);
        connect(&timer, &QTimer::timeout, this, [this] {
            if (!queue.pending() || !this->canvas->isVisible())
                return;
            const auto ratio = this->canvas->viewport()->devicePixelRatioF();
            const auto origin = QPointF(this->canvas->viewport()->mapToGlobal(QPoint())) * ratio;
            const auto pixels = QRectF(queue.nextArea()).translated(-origin);
            const auto logical = QRectF(pixels.topLeft() / ratio, pixels.size() / ratio).toAlignedRect();
            QScopedValueRollback<bool> presenting(presentingRegion, true);
            this->canvas->viewport()->repaint(logical);
        });
        canvas->viewport()->installEventFilter(this);
        canvas->framePreparing = [this](const QRegion &region) {
            accepted = false;
            auto *content = this->canvas->content.data();
            if (!content || !this->available() || region.isEmpty())
                return;
            if (presentingRegion) {
                accepted = queue.present(this->submit);
                return;
            }
            auto *page = static_cast<ReadingPage *>(content->findChild<QWidget *>("rrPage"));
            accepted = policy.prepare(content, this->canvas->viewport(), page, region, [this](RefreshFrame frame) {
                frame.clean = frame.clean || cleanPending;
                if (!queue.prepare(frame, this->submit))
                    return false;
                cleanPending = false;
                return true;
            });
        };
        canvas->framePainted = [this](const QRegion &) {
            if (accepted && queue.pending() && !timer.isActive())
                timer.start();
        };
    }
    void requestClean() { cleanPending = true; }

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            timer.stop();
            queue.reset();
            policy.reset();
            accepted = false;
            cleanPending = cleanOnReveal;
        }
        return false;
    }

private:
    WidgetCanvas *canvas;
    std::function<bool()> available;
    std::function<bool(const RefreshFrame &)> submit;
    BooxRefreshPolicy policy;
    BooxFrameQueue queue;
    QTimer timer;
    bool accepted = false, presentingRegion = false;
    bool cleanOnReveal, cleanPending;
};
}
