#pragma once
#include "platform/api/window_presentation.h"
#include <QDialog>
#include <memory>

namespace kobalt::test {
// A desktop test window has no library backing store to pause or cover, so a
// handoff accepts the dialog directly.
class HostPresentation final : public WindowPresentation {
public:
    void pauseBackground(bool) override {}
    void resumeBackground() override {}
    void resumeAndRepaintBackground() override {}
    void acceptHandoff(QDialog &, const std::function<void()> &accept) override {
        accept();
    }
};
inline std::unique_ptr<WindowPresentation> makeHostPresentation(QWidget *) {
    return std::make_unique<HostPresentation>();
}
}
