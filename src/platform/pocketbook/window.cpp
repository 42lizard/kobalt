#include "window.h"

#include "device.h"
#include "interfaces/reader/ui/style/scale.h"
#include "interfaces/reader/ui/style/theme.h"
#include "platform/android/canvas.h"
#include "platform/android/start_screen.h"
#include "reading/reading_density.h"

#include <QCloseEvent>
#include <QFileDialog>
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
    start = new android::StartScreen(readerServices().presentation, this);
    canvas = new android::WidgetCanvas(this);
    stack->addWidget(start);
    stack->addWidget(canvas);
    start->setLastBook(controller.lastBook());
    connect(start, &android::StartScreen::openRequested, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open EPUB"), "/mnt/ext1", tr("EPUB books (*.epub)"),
                                                       nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            openBook(path);
        }
    });
    connect(start, &android::StartScreen::reopenRequested, this, &Window::openBook);
    connect(start, &android::StartScreen::forgetRequested, this, [this] {
        if (controller.forgetLastBook()) {
            start->setLastBook({});
        } else {
            start->showForgetError();
        }
    });
    connect(start, &android::StartScreen::exitRequested, this, &QWidget::close);
    connect(&controller, &android::ReaderController::openingProgress, start, &android::StartScreen::setProgress);
    connect(&controller, &android::ReaderController::opened, this, [this] {
        auto *reader = controller.window();
        canvas->setContent(reader);
        stack->setCurrentWidget(canvas);
        canvas->fitContent();
        reader->show();
        start->setLastBook(controller.lastBook());
        reader->checkpoint();
        keepAwake(false);
        emit bookOpened();
    });
    connect(&controller, &android::ReaderController::failed, this, [this](const QString &error) {
        failed = true;
        start->showError(error);
        stack->setCurrentWidget(start);
        keepAwake(false);
        emit bookFailed(error);
    });
    connect(&controller, &android::ReaderController::closed, this, [this] {
        stack->setCurrentWidget(start);
        if (!failed) {
            start->showReady();
        }
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
    failed = false;
    keepAwake(true);
    start->showOpening();
    stack->setCurrentWidget(start);
    controller.open(QFileInfo(path).absoluteFilePath(), size());
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
