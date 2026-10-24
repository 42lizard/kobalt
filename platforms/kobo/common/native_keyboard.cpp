#include "native_keyboard.h"
#include "interfaces/reader/ui/style/theme.h"
#include <QApplication>
#include <QGraphicsEffect>
#include <QImage>
#include <QPainter>

namespace kobalt {
void (*nativeKeyboardRepaint)(QWidget *, bool) = nullptr;
namespace {
class KeyboardTheme final : public QGraphicsEffect {
public:
    explicit KeyboardTheme(QWidget *frame) : QGraphicsEffect(frame), frame(frame) {}

protected:
    void draw(QPainter *painter) override {
        const bool dark = ui::darkTheme(frame);
        if (!dark) {
            drawSource(painter);
            return;
        }
        QPoint offset;
        const auto source = sourcePixmap(Qt::DeviceCoordinates, &offset, NoPad);
        auto image = source.toImage().convertToFormat(QImage::Format_ARGB32);
        image.invertPixels(QImage::InvertRgb);
        painter->save();
        painter->setWorldTransform(QTransform());
        painter->drawImage(offset, image);
        painter->restore();
    }

private:
    QWidget *frame;
};
}
void styleNativeKeyboard(QWidget *frame) {
    frame->setProperty("reader_native_colors", true);
    auto palette = ui::readerPalette(QApplication::palette(), false);
    // Resolve every role so native icons cannot inherit the reader's dark ink.
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        for (int role = 0; role < QPalette::NColorRoles; ++role)
            palette.setBrush(group, static_cast<QPalette::ColorRole>(role),
                             palette.brush(group, static_cast<QPalette::ColorRole>(role)));
    frame->setPalette(palette);
    // Nickel paints literal black and white instead of using its palette.
    // Invert its complete output so labels, icons and press feedback agree.
    if (nativeKeyboardRepaint)
        frame->setGraphicsEffect(new KeyboardTheme(frame));
}
}

extern "C" __attribute__((visibility("default"))) void kobalt_keyboard_repaint(QWidget *widget, bool fast) {
    for (auto *parent = widget; parent; parent = parent->parentWidget()) {
        if (parent->objectName() == "rrSearchKeyboard") {
            // Nickel's immediate key repaint bypasses widget effects. Let Qt
            // repaint the keyboard through its theme effect instead.
            widget->update();
            return;
        }
    }
    kobalt::nativeKeyboardRepaint(widget, fast);
}
