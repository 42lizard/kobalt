#include "reader.h"
#include "start_screen.h"
#include "services.h"
#include "runtime.h"
#include "canvas.h"
#include "assets.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/views/reading_page.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "support/diagnostics.h"
#include "support/strings.h"
#include "reading/reading_density.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPushButton>
#include <QSettings>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTimer>

using namespace kobalt;
namespace {
class Window : public QWidget {
public:
    Window(EngineFontInputs fonts, QString placeholder)
        : controller(storage::Paths{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config"},
                     std::move(fonts), std::move(placeholder), android::readerServices,
                     [](Qt::ScreenOrientation) { /* Android owns display rotation. */ }) {
        setWindowTitle("Kobalt");
        setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea);
        QPalette colors;
        colors.setColor(QPalette::Window, Qt::white);
        colors.setColor(QPalette::WindowText, Qt::black);
        colors.setColor(QPalette::Base, Qt::white);
        colors.setColor(QPalette::Text, Qt::black);
        setPalette(colors);
        setAutoFillBackground(true);
        layout = new QStackedLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        start = new android::StartScreen(android::readerServices().presentation);
        connect(start, &android::StartScreen::openRequested, this, [] {
            android::activity("chooseBook");
        });
        connect(start, &android::StartScreen::exitRequested, this, &QWidget::close);
        auto last = controller.lastBook();
        if (last.isEmpty()) {
            const auto legacy = QSettings().value("lastBook").toString();
            if (QFileInfo(legacy).isFile() && QFileInfo(legacy).isReadable())
                last = {{"path", legacy}, {"title", strings::get("android.previous_book")}};
        }
        start->setLastBook(last);
        connect(start, &android::StartScreen::reopenRequested, this, [this](const QString &path) {
            openBook(path);
        });
        connect(start, &android::StartScreen::forgetRequested, this, [this] {
            if (!controller.forgetLastBook()) {
                start->showForgetError();
                return;
            }
            QSettings settings;
            settings.remove("lastBook");
            settings.sync();
            start->setLastBook({});
        });
        layout->addWidget(start);
        canvas = new android::WidgetCanvas(this);
        layout->addWidget(canvas);
        canvas->viewportResized = [this] {
            sizeCanvas();
        };
        openingHandoff.setInterval(16);
        connect(&openingHandoff, &QTimer::timeout, this, [this] {
            auto *reader = controller.window();
            if (closing || !reader) {
                openingHandoff.stop();
                return;
            }
            sizeCanvas();
            if (!android::readingGeometryReady() || !reader->readyForPresentation())
                return;
            // Discard any proxy pixels prepared while the canvas was covered.
            QCoreApplication::sendPostedEvents(reader, QEvent::UpdateLater);
            QCoreApplication::sendPostedEvents(reader, QEvent::UpdateRequest);
            reader->graphicsProxyWidget()->update();
            layout->setCurrentWidget(canvas);
            layout->setStackingMode(QStackedLayout::StackOne);
            canvas->viewport()->repaint();
            openingHandoff.stop();
            syncPageButtons();
            reader->checkpoint();
            qInfo() << "Kobalt frame ready";
        });
        qApp->installEventFilter(this);
        android::setCallbacks(
            this,
            [this](const QString &path) {
                openBook(path);
            },
            [this](int direction) {
                if (!pageButtonsReady())
                    return;
                controller.window()->pageButton(direction);
            });
        canvas->cancelGesture = [this] {
            if (auto *reader = controller.window())
                if (auto *page = static_cast<ReadingPage *>(reader->findChild<QWidget *>("rrPage")))
                    page->cancelHold();
        };
        connect(qApp, &QCoreApplication::aboutToQuit, this, [] {
            android::setPageButtonsAvailable(false);
        });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            syncPageButtons();
            if (state != Qt::ApplicationActive && controller.window())
                controller.window()->checkpoint();
        });
        connect(&controller, &android::ReaderController::openingProgress, start, &android::StartScreen::setProgress);
        connect(&controller, &android::ReaderController::opened, this, [this] {
            auto *reader = controller.window();
            canvas->setContent(reader);
            setPalette(reader->palette());
            sizeCanvas();
            for (const auto *name : {"rrLight", "rrEdgeLight", "rrScreenRefresh"})
                if (auto *control = reader->findChild<QWidget *>(name))
                    control->hide();
            if (auto *exit = reader->findChild<QPushButton *>("rrReturn")) {
                exit->setText(strings::get("android.return"));
                exit->setAccessibleName(strings::get("android.return"));
            }
            reader->show();
            setUpdatesEnabled(true);
            openingHandoff.start();
            start->setLastBook(controller.lastBook());
            QSettings settings;
            settings.remove("lastBook");
            settings.sync();
        });
        connect(&controller, &android::ReaderController::failed, this, [this](const QString &error) {
            failedOpening = true;
            android::setPageButtonsAvailable(false);
            openingHandoff.stop();
            layout->setStackingMode(QStackedLayout::StackOne);
            setPalette(start->palette());
            android::activity("showStartup");
            start->showError(error);
            layout->setCurrentWidget(start);
            qWarning() << "Kobalt reader error:" << error;
        });
        connect(&controller, &android::ReaderController::closed, this, [this] {
            android::setPageButtonsAvailable(false);
            if (closing) {
                android::activity("exitReader");
                QCoreApplication::quit();
                return;
            }
            openingHandoff.stop();
            layout->setStackingMode(QStackedLayout::StackOne);
            setPalette(start->palette());
            android::activity("showStartup");
            start->setLastBook(controller.lastBook());
            if (!failedOpening)
                start->showReady();
            layout->setCurrentWidget(start);
        });
        QTimer::singleShot(0, this, [this] {
            android::activity("readerReady");
        });
    }
    ~Window() override {
        android::setCallbacks(nullptr, {}, {});
    }

private:
    android::ReaderController controller;
    QStackedLayout *layout;
    android::StartScreen *start;
    android::WidgetCanvas *canvas;
    QTimer openingHandoff;
    bool closing = false, layoutRepairPending = false, failedOpening = false;
    QSize pixels() const {
        if (canvas->isVisible())
            return canvas->pixelSize();
        // The loading page occupies the same safe content area as the canvas.
        // A hidden canvas can still have its old viewport size until shown.
        const auto area = contentsRect().size();
        return {qRound(area.width() * devicePixelRatioF()), qRound(area.height() * devicePixelRatioF())};
    }
    void sizeCanvas() {
        canvas->fitContent();
    }
    bool pageButtonsReady() const {
        auto *reader = controller.window();
        if (closing || !reader || openingHandoff.isActive() || layout->currentWidget() != canvas ||
            !reader->isEnabled() || QGuiApplication::applicationState() != Qt::ApplicationActive)
            return false;
        auto *list = reader->findChild<QWidget *>("rrBookmarkList");
        return !list || !list->isVisible();
    }
    void syncPageButtons() {
        android::setPageButtonsAvailable(pageButtonsReady());
    }
    void openBook(const QString &path) {
        if (closing)
            return;
        if (controller.window() && controller.path() == path)
            return;
        failedOpening = false;
        android::setPageButtonsAvailable(false);
        openingHandoff.stop();
        start->showOpening();
        layout->setCurrentWidget(start);
        // Keep the canvas laid out behind the opaque loader as insets change.
        layout->setStackingMode(QStackedLayout::StackAll);
        android::activity("showReading");
        layout->activate();
        controller.open(path, pixels());
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *reader = controller.window();
        if (reader && (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
                       event->type() == QEvent::EnabledChange)) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (widget && (widget == reader || reader->isAncestorOf(widget)))
                syncPageButtons();
        }
        if (reader && object == reader && event->type() == QEvent::PaletteChange && palette() != reader->palette())
            setPalette(reader->palette());
        if (reader && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (widget && (widget == reader || reader->isAncestorOf(widget)) && !layoutRepairPending) {
                layoutRepairPending = true;
                QTimer::singleShot(0, this, [this] {
                    layoutRepairPending = false;
                    auto *reader = controller.window();
                    if (!reader)
                        return;
                    // Proxy resize can leave newly created children with an
                    // inherited disabled-update flag. Preserve explicit disables.
                    for (auto *child : reader->findChildren<QWidget *>())
                        if (!child->updatesEnabled() && !child->testAttribute(Qt::WA_ForceUpdatesDisabled) &&
                            child->parentWidget() && child->parentWidget()->updatesEnabled())
                            child->setUpdatesEnabled(true);
                });
            }
        }
        return QWidget::eventFilter(object, event);
    }
    void closeEvent(QCloseEvent *event) override {
        // Qt closes top-level windows while quitting. Accept that request
        // once reader work has drained, or the event loop cannot exit.
        if (closing && controller.idle()) {
            event->accept();
            return;
        }
        event->ignore();
        if (closing)
            return;
        if (start->dismissModal())
            return;
        if (auto *reader = controller.window()) {
            android::setPageButtonsAvailable(false);
            reader->close();
            return;
        }
        if (!controller.idle()) {
            controller.close();
            return;
        }
        closing = true;
        controller.close();
    }
};
}
int main(int argc, char **argv) {
    FontCopies copies;
    QApplication app(argc, argv);
    app.setApplicationName("Kobalt");
    app.setOrganizationName("Kobalt");
    app.setQuitOnLastWindowClosed(false);
    const auto root = android::installAssets();
    AssetBundle assets(root, QStringLiteral(KOBALT_VERSION));
    if (root.isEmpty() || !assets.available() || !ui::initializeInterfaceFonts(assets)) {
        qCritical() << "Kobalt bundled assets are unavailable";
        return 2;
    }
    for (const auto &file : bundledFontFiles(assets))
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical() << "Cannot register font" << file;
            return 2;
        }
    const auto physicalPpi = android::screenDensity();
    readingPpi.store(physicalPpi);
    ui::setDensity(physicalPpi);
    QApplication::setFont(ui::interfaceFont());
    initializeDiagnostics(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/diagnostics");
    Window window(engineFontInputs(assets, "/system/fonts", {}, {}), placeholderFontFamily(assets, copies));
    window.showFullScreen();
    qInfo() << "Kobalt Android ready; density" << physicalPpi;
    return app.exec();
}
