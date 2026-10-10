#pragma once
#include <functional>
#include <memory>
class QDialog;
class QWidget;

namespace kobalt {
class WindowPresentation {
public:
    virtual ~WindowPresentation() = default;
    virtual void pauseBackground(bool waking = false) = 0;
    virtual void resumeBackground() = 0;
    virtual void resumeAndRepaintBackground() = 0;
    // The continuation accepts the dialog. The host keeps the appropriate
    // backing store covered while that continuation installs its reader.
    virtual void acceptHandoff(QDialog &dialog, const std::function<void()> &accept) = 0;
};
using PresentationFactory = std::function<std::unique_ptr<WindowPresentation>(QWidget *)>;
}
