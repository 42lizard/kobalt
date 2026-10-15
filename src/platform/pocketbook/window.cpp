#include "window.h"

#include "device.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/ui/style/theme.h"
#include "interfaces/reader/views/opening.h"
#include "platform/android/canvas.h"
#include "reading/reading_density.h"
#include "support/diagnostics.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QLineEdit>
#include <QStackedLayout>

namespace kobalt::pocketbook {
Window::Window(storage::Paths paths, EngineFontInputs fonts, QString placeholder)
    : controller(std::move(paths), std::move(fonts), std::move(placeholder), readerServices,
                 [this](Qt::ScreenOrientation orientation) {
                     resize(rotate(orientation));
                 }) {
    setWindowTitle("Kobalt");
    readingPpi.store(300);
    ui::setDensity(300);
    stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);
    opening = createEmbeddedOpening(this, readerServices().presentation);
    canvas = new android::WidgetCanvas(this);
    stack->addWidget(opening->window());
    stack->addWidget(canvas);
    connect(opening->window(), &QDialog::rejected, this, &QWidget::close);
    connect(opening->window(), &QDialog::accepted, this, [this] {
        if (openNativeBook(bookPath)) {
            close();
        } else {
            diagnostic("pocketbook-native-open-failed");
            opening->window()->show();
        }
    });
    connect(&controller, &android::ReaderController::openingProgress, this, [this](int percent) {
        opening->setProgress(percent);
    });
    connect(&controller, &android::ReaderController::exitRequested, this, &QWidget::close);
    connect(&controller, &android::ReaderController::opened, this, [this] {
        auto *reader = controller.window();
        canvas->setContent(reader);
        stack->setCurrentWidget(canvas);
        canvas->fitContent();
        reader->show();
        reader->checkpoint();
        bookReady(bookPath, controller.lastBook().value("title").toString());
        diagnostic("pocketbook-book-opened");
        keepAwake(false);
        emit bookOpened();
    });
    connect(&controller, &android::ReaderController::failed, this, [this](const QString &error) {
        diagnostic("pocketbook-book-failed");
        opening->showFallback(OpeningFallback::Failure);
        stack->setCurrentWidget(opening->window());
        keepAwake(false);
        emit bookFailed(error);
    });
    connect(&controller, &android::ReaderController::closed, this, [this] {
        keepAwake(false);
        if (closing) {
            close();
        }
    });
    canvas->viewportResized = [this] {
        canvas->fitContent();
    };
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (auto *reader = controller.window()) {
            if (state == Qt::ApplicationActive) {
                reader->prepareWake();
                reader->wakeReader();
            } else {
                reader->checkpoint();
                reader->sleepReader();
            }
        }
    });
    qApp->installEventFilter(this);
}
void Window::openBook(const QString &path) {
    if (closing || (controller.window() && controller.path() == QFileInfo(path).absoluteFilePath())) {
        return;
    }
    bookPath = QFileInfo(path).absoluteFilePath();
    diagnostic("pocketbook-book-requested");
    keepAwake(true);
    stack->setCurrentWidget(opening->window());
    controller.open(bookPath, size());
}
void Window::closeEvent(QCloseEvent *event) {
    if (!controller.idle()) {
        event->ignore();
        if (closing) {
            return;
        }
        closing = true;
        keepAwake(true);
        controller.close();
    } else {
        keepAwake(false);
        event->accept();
    }
}
void Window::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    canvas->fitContent();
}
bool Window::eventFilter(QObject *object, QEvent *event) {
    auto *reader = controller.window();
    if (reader && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        auto *widget = qobject_cast<QWidget *>(object);
        if (widget && (widget == reader || reader->isAncestorOf(widget)) && !repairPending) {
            repairPending = true;
            QTimer::singleShot(0, this, [this] {
                repairPending = false;
                if (auto *reader = controller.window()) {
                    // Qt proxies can disable updates on new children after resizing.
                    // Preserve explicit disables made by reader controls.
                    for (auto *child : reader->findChildren<QWidget *>()) {
                        if (!child->updatesEnabled() && !child->testAttribute(Qt::WA_ForceUpdatesDisabled) &&
                            child->parentWidget() && child->parentWidget()->updatesEnabled()) {
                            child->setUpdatesEnabled(true);
                        }
                    }
                }
            });
        }
    }
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool back = key->key() == Qt::Key_Back || key->key() == Qt::Key_Escape;
        if (back && QApplication::activeModalWidget()) {
            return QWidget::eventFilter(object, event);
        }
        if (key->key() == Qt::Key_Home || back) {
            // Handle the press: a modal dismissed by Escape must not make its
            // following release become a second, application-wide close.
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                close();
            }
            return true;
        }
        const auto direction = readerServices().pageButtonDirection(key);
        if (direction && controller.window()) {
            if (QApplication::activeModalWidget() || qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
                return QWidget::eventFilter(object, event);
            }
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                controller.window()->pageButton(direction);
            }
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
}
