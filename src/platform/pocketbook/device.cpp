#include "device.h"

#include "platform/api/kobo_refresh.h"
#include "platform/desktop/services.h"

#include <QApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <cerrno>

#ifndef KOBALT_POCKETBOOK_HOST
#include <inkview.h>
#endif

namespace kobalt::pocketbook {
QSize initialize() {
#ifdef KOBALT_POCKETBOOK_HOST
    return {1404, 1872};
#else
    InitInkview(TASK_MAKEACTIVE);
    return {ScreenWidth(), ScreenHeight() - PanelHeight()};
#endif
}
void keepAwake(bool active) {
#ifndef KOBALT_POCKETBOOK_HOST
    if (!active) {
        BanSleep(1);
    }
    iv_sleepmode(active ? 0 : 1);
#else
    Q_UNUSED(active);
#endif
}
QSize rotate(Qt::ScreenOrientation orientation) {
#ifndef KOBALT_POCKETBOOK_HOST
    switch (orientation) {
    case Qt::PortraitOrientation:
        SetOrientation(ROTATE0);
        break;
    case Qt::LandscapeOrientation:
        SetOrientation(ROTATE90);
        break;
    case Qt::InvertedPortraitOrientation:
        SetOrientation(ROTATE180);
        break;
    case Qt::InvertedLandscapeOrientation:
        SetOrientation(ROTATE270);
        break;
    default:
        break;
    }
    return {ScreenWidth(), ScreenHeight() - PanelHeight()};
#else
    const bool landscape = orientation == Qt::LandscapeOrientation || orientation == Qt::InvertedLandscapeOrientation;
    return landscape ? QSize(1872, 1404) : QSize(1404, 1872);
#endif
}
namespace {
class Keyboard final : public KeyboardController {
public:
    explicit Keyboard(QLineEdit *editor) : editor(editor) {}
    bool show() override {
        if (!editor) {
            return false;
        }
        editor->setFocus();
        QGuiApplication::inputMethod()->show();
        return true;
    }
    void hide() override {
        QGuiApplication::inputMethod()->hide();
    }
    void place() override {}

private:
    QPointer<QLineEdit> editor;
};
}
ReaderHostServices readerServices() {
    auto services = desktop::readerServices();
    services.desktopUi = false;
    services.downloadableDictionaries = false;
    services.browserAvailable = [] {
        return false;
    };
    services.openBrowser = [](const QUrl &, QWidget *) {
        return false;
    };
    services.pageButtons = [] {
        return true;
    };
    services.pageButtonDirection = [](QKeyEvent *event) {
        switch (event->key()) {
        case Qt::Key_PageDown:
            return 1;
        case Qt::Key_PageUp:
            return -1;
        default:
            return 0;
        }
    };
    services.keyboard = [](QWidget *, QLineEdit *editor) {
        return std::make_unique<Keyboard>(editor);
    };
#ifndef KOBALT_POCKETBOOK_HOST
    services.battery = [] {
        return BatteryStatus{GetBatteryPower(), IsCharging() != 0};
    };
#endif
    return services;
}
}
namespace kobalt::platform {
// The PocketBook QPA owns paint/partial refresh. Request a clean update after
// the queued Qt paint for the reader's explicit clean-refresh boundary.
void textRefresh(QWidget *widget) {
    if (widget) {
        widget->update();
    }
}
void pageRefresh(QWidget *widget, bool clean, bool) {
    if (!widget) {
        return;
    }
    widget->update();
#ifndef KOBALT_POCKETBOOK_HOST
    if (clean) {
        QTimer::singleShot(200, widget, [] {
            FullUpdate();
        });
    }
#else
    Q_UNUSED(clean);
#endif
}
void setReaderRefreshWindow(QWidget *) {}
}
namespace kobalt::nickel4 {
bool configureLoopback(int *error) {
    if (error) {
        *error = ENOTSUP;
    }
    return false;
}
}
