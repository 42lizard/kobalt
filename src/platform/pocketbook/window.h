#pragma once
#include "interfaces/reader_interface.h"
#include "platform/android/reader.h"

#include <QWidget>

class QStackedLayout;
namespace kobalt::android {
class WidgetCanvas;
}
namespace kobalt::pocketbook {
class Window final : public QWidget {
    Q_OBJECT
public:
    Window(storage::Paths paths, EngineFontInputs fonts, QString placeholder);
    void openBook(const QString &path);
    bool idle() const {
        return controller.idle();
    }
signals:
    void bookOpened();
    void bookFailed(QString error);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    android::ReaderController controller;
    QStackedLayout *stack;
    std::unique_ptr<ReaderOpeningView> opening;
    QString bookPath;
    android::WidgetCanvas *canvas;
    bool closing = false;
    bool repairPending = false;
};
}
