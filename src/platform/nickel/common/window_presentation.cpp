#include "window_presentation.h"
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QVector>

namespace kobalt::nickel {
namespace {
class Presentation final : public WindowPresentation {
public:
    explicit Presentation(QWidget *reader) : reader(reader) {}
    ~Presentation() override {
        resumeBackground();
    }
    void pauseBackground(bool waking) override {
        if (!reader)
            return;
        for (auto *window : QApplication::topLevelWidgets()) {
            // During wake only pause the library/sleep backing store. Native
            // PIN and USB windows must remain able to draw and respond.
            if (waking && !window->inherits("MainWindowView"))
                continue;
            if (window != reader && !reader->isAncestorOf(window) && window->isVisible() && window->updatesEnabled()) {
                paused.append(window);
                window->setUpdatesEnabled(false);
            }
        }
    }
    void resumeBackground() override {
        for (const auto &window : paused)
            if (window)
                window->setUpdatesEnabled(true);
        paused.clear();
    }
    void resumeAndRepaintBackground() override {
        const auto windows = paused;
        resumeBackground();
        for (const auto &window : windows)
            if (window && window->isVisible()) {
                // Fold the queued child paints into the backing store before the
                // reader leaves the compositor and exposes this window.
                for (auto *child : window->findChildren<QWidget *>())
                    QCoreApplication::sendPostedEvents(child, QEvent::UpdateLater);
                QCoreApplication::sendPostedEvents(window, QEvent::UpdateLater);
                QCoreApplication::sendPostedEvents(window, QEvent::UpdateRequest);
                window->repaint();
            }
    }
    void acceptHandoff(QDialog &dialog, const std::function<void()> &accept) override {
        if (!dialog.parentWidget()) {
            accept();
            return;
        }
        // Nickel closes the dialog before installing its reader. Cover the
        // library's backing store until that synchronous continuation returns.
        QPointer<QWidget> background = dialog.parentWidget();
        QPointer<QLabel> cover = new QLabel(background);
        cover->setObjectName("rrHandoffCover");
        cover->setGeometry(background->rect());
        cover->setPixmap(dialog.grab());
        cover->show();
        cover->raise();
        cover->repaint();
        accept();
        delete cover.data();
        if (background && background->isVisible())
            background->repaint();
    }

private:
    QPointer<QWidget> reader;
    QVector<QPointer<QWidget>> paused;
};
}
std::unique_ptr<WindowPresentation> makeWindowPresentation(QWidget *reader) {
    return std::make_unique<Presentation>(reader);
}
}
