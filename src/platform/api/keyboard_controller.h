#pragma once
#include <functional>
#include <memory>
class QWidget;
class QLineEdit;

namespace kobalt {
// Destroy before the editor and panel. The host owns any native keyboard
// widgets; the reader owns this controller, not those widgets.
class KeyboardController {
public:
    virtual ~KeyboardController() = default;
    virtual bool show() = 0;
    virtual void hide() = 0;
    virtual void place() = 0;
    std::function<void()> submitted;
};
using KeyboardFactory = std::function<std::unique_ptr<KeyboardController>(QWidget *, QLineEdit *)>;
}
