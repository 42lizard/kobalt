#pragma once
#include "platforms/android/reader.h"
#include <QMainWindow>
#include <QTimer>

#include <functional>

class QStackedWidget;
class QComboBox;
class QScreen;
class QActionGroup;
class QAction;
namespace kobalt::android {
class WidgetCanvas;
class StartScreen;
}
namespace kobalt::desktop {
class DeviceFrame;
class Window final : public QMainWindow {
    Q_OBJECT
public:
    Window(storage::Paths paths, EngineFontInputs fonts, QString placeholder, std::function<QString()> clock = {});
    void openBook(const QString &path);
    // Saves the visible preview without Mac chrome. Device surroundings remain transparent.
    // Returns the new PNG path, or an empty string and an error. Existing files are never replaced.
    QString saveScreenshot(const QString &directory, QString *error = nullptr);
    ReaderWindow *readerWindow() const {
        return controller.window();
    }
    bool idle() const {
        return controller.idle();
    }
signals:
    void bookOpened();
    void bookFailed(QString error);
    void drained();

protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    const std::function<QString()> clockSource;
    // This controller and canvas contain no Android API calls. Reuse their
    // publication lifetime and physical-pixel rendering on the desktop.
    android::ReaderController controller;
    QStackedWidget *stack;
    DeviceFrame *deviceFrame;
    android::StartScreen *start;
    android::WidgetCanvas *canvas;
    QComboBox *screenPicker, *resolutionPicker;
    QActionGroup *screenActions, *resolutionActions;
    QAction *rotateAction;
    int screenPreset = 1, renderingDpi = 300;
    QTimer resizeTimer;
    QMetaObject::Connection screenChanged, logicalDpiChanged, physicalDpiChanged;
    bool closing = false, repairPending = false, failed = false;
    QSize pixels() const;
    void sizeCanvas();
    void watchScreen(QScreen *screen);
    void selectScreen(int index);
    void setPreviewControlsEnabled(bool enabled);
    void chooseBook();
    void turnPage(int direction);
};
}
