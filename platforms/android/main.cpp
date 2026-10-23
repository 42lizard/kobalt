#include "reader.h"
#include "start_screen.h"
#include "services.h"
#include "runtime.h"
#include "canvas.h"
#include "refresh.h"
#include "boox_frame_queue.h"
#include "boox_refresh.h"
#include "assets.h"
#include "library_source.h"
#include "interfaces/library/library_view.h"
#include "library/library_store.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/views/reading_page.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "support/diagnostics.h"
#include "support/strings.h"
#include "reading/reading_density.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPushButton>
#include <QSettings>
#include <QScopedValueRollback>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTimer>
#include <utility>

using namespace kobalt;
namespace {
class Window : public QWidget {
public:
    Window(EngineFontInputs fonts, QString placeholder)
        : controller(storage::Paths{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config"},
                     std::move(fonts), std::move(placeholder), android::readerServices,
                     [](Qt::ScreenOrientation) { /* Android owns display rotation. */ }),
          store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config",
                std::make_shared<android::LibrarySource>()) {
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
        start = new android::StartScreen(android::readerServices().presentation, store);
        // A book picked while the library is shown joins it; see openBook.
        connect(start, &android::StartScreen::libraryOpenRequested, this, [] {
            android::activity("chooseBook");
        });
        connect(start, &android::StartScreen::addFolderRequested, this, [] {
            android::activity("chooseFolder");
        });
        connect(start, &android::StartScreen::exitRequested, this, &QWidget::close);
        connect(start, &android::StartScreen::bookRequested, this, &Window::openLibraryBook);
        connect(start, &android::StartScreen::removeFolderRequested, this, [this](const QString &key) {
            store.removeFolder(key);
            android::releaseFolder(key);
        });
        connect(start, &android::StartScreen::removeBookRequested, this, [this](const QString &key) {
            const auto book = store.index().books.value(key);
            // A book opened on its own is Kobalt's copy; delete it with the entry.
            if (book.folder.isEmpty() &&
                QFileInfo(QFileInfo(key).absolutePath()).canonicalFilePath() == importedBooks())
                QFile::remove(key);
            store.removeBook(key);
        });
        start->setSingleBooksAreCopies(true);
        start->setDark(controller.preferences().value("dark_mode").toBool(false));
        layout->addWidget(start);
        canvas = new android::WidgetCanvas(this);
        layout->addWidget(canvas);
        canvas->viewportResized = [this] {
            resetRefresh();
            sizeCanvas();
        };
        canvas->framePreparing = [this](const QRegion &region) {
            regionAccepted = false;
            auto *reader = controller.window();
            if (!reader || layout->currentWidget() != canvas || !android::refreshAvailable() || region.isEmpty()) {
                return;
            }
            if (presentingRegion) {
                regionAccepted = refreshQueue.present(submitRefresh);
                return;
            }
            auto *page = static_cast<ReadingPage *>(reader->findChild<QWidget *>("rrPage"));
            regionAccepted = booxRefresh.prepare(reader, canvas->viewport(), page, region,
                                                 [this](const android::RefreshFrame &frame) {
                                                     return refreshQueue.prepare(frame, submitRefresh);
                                                 });
        };
        regionFrame.setSingleShot(true);
        // Keep the next region out of this compositor frame. Two 60 Hz frames
        // also leave room for Qt's queued backing-store flush.
        regionFrame.setInterval(34);
        connect(&regionFrame, &QTimer::timeout, this, [this] {
            if (!refreshQueue.pending() || !canvas->isVisible()) {
                return;
            }
            const auto ratio = canvas->viewport()->devicePixelRatioF();
            const auto origin = QPointF(canvas->viewport()->mapToGlobal(QPoint())) * ratio;
            const QRectF pixels = QRectF(refreshQueue.nextArea()).translated(-origin);
            const QRect logical = QRectF(pixels.topLeft() / ratio, pixels.size() / ratio).toAlignedRect();
            QScopedValueRollback<bool> presenting(presentingRegion, true);
            canvas->viewport()->repaint(logical);
        });
        canvas->framePainted = [this](const QRegion &) {
            if (regionAccepted && refreshQueue.pending() && !regionFrame.isActive()) {
                regionFrame.start();
            }
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
            },
            [this](const QString &uri, const QString &label) {
                store.addFolder({uri, label});
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
            if (!android::refreshAvailable())
                if (auto *control = reader->findChild<QWidget *>("rrScreenRefresh"))
                    control->hide();
            if (auto *exit = reader->findChild<QPushButton *>("rrReturn")) {
                exit->setText(strings::get("android.return"));
                exit->setAccessibleName(strings::get("android.return"));
            }
            reader->show();
            setUpdatesEnabled(true);
            openingHandoff.start();
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
            start->showLibraryError(error);
            layout->setCurrentWidget(start);
            qWarning() << "Kobalt reader error:" << error;
        });
        connect(&controller, &android::ReaderController::closed, this, [this] {
            resetRefresh();
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
            recordProgress();
            start->setDark(controller.preferences().value("dark_mode").toBool(false));
            if (!failedOpening)
                start->showLibrary();
            layout->setCurrentWidget(start);
        });
        QTimer::singleShot(0, this, [this] {
            android::activity("readerReady");
            if (store.index().settings.checkAtStart)
                store.scan();
        });
    }
    ~Window() override {
        android::setCallbacks(nullptr, {}, {});
    }

private:
    android::ReaderController controller;
    library::Store store;
    QStackedLayout *layout;
    android::StartScreen *start;
    // The library entry of the book being read, and the local file it opened.
    QString openedKey, openedPath;
    android::WidgetCanvas *canvas;
    android::BooxRefreshPolicy booxRefresh;
    android::BooxFrameQueue refreshQueue;
    QTimer regionFrame;
    bool presentingRegion = false, regionAccepted = false;
    static bool submitRefresh(const android::RefreshFrame &frame) {
        return android::prepareRefreshRegions(frame.screenPixels - frame.textPixels, frame.textPixels, frame.clean);
    }
    void resetRefresh() {
        regionFrame.stop();
        refreshQueue.reset();
        booxRefresh.reset();
        regionAccepted = false;
    }
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
    static QString appData() {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    // The activity copies picked and shared files to getFilesDir()/books,
    // which is Qt's application data folder on Android.
    static QString importedBooks() {
        return QFileInfo(appData() + "/books").canonicalFilePath();
    }
    QString cachePath(const QString &uri) const {
        return appData() + "/library-cache/" +
               QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Sha1).toHex()) + ".epub";
    }
    void openLibraryBook(const QString &key) {
        const auto book = store.index().books.value(key);
        if (book.key.isEmpty() || closing)
            return;
        if (book.folder.isEmpty()) {
            openBook(key);
            return;
        }
        // Library books are read in place; the reader opens a cached copy.
        pendingKey = key;
        pendingPath = cachePath(key);
        start->showOpening();
        android::openLibraryBook(key, pendingPath, book.size, book.modified);
    }
    void recordProgress() {
        if (openedKey.isEmpty())
            return;
        const auto progress = library::savedProgress(controller.savedState(openedPath));
        store.setProgress(std::exchange(openedKey, {}), progress.percent, progress.finished);
    }
    QString pendingKey, pendingPath;
    void openBook(const QString &path) {
        if (closing)
            return;
        recordProgress();
        if (path.isEmpty()) {
            // The activity could not copy a library book; it said why.
            pendingKey.clear();
            pendingPath.clear();
            start->showLibrary();
            return;
        }
        if (path == pendingPath && !pendingKey.isEmpty()) {
            openedKey = std::exchange(pendingKey, {});
        } else {
            // Document picking and Android Open With use the same library path.
            const auto key = QFileInfo(path).absoluteFilePath();
            if (!store.index().books.contains(key))
                store.addBook(path);
            openedKey = store.index().books.contains(key) ? key : QString();
        }
        pendingPath.clear();
        openedPath = path;
        if (!openedKey.isEmpty())
            store.setOpened(openedKey);
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
    const auto assets = android::installAssets(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!assets || !ui::initializeInterfaceFonts(*assets)) {
        qCritical() << "Kobalt bundled assets are unavailable";
        return 2;
    }
    for (const auto &file : bundledFontFiles(*assets))
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical() << "Cannot register font" << file;
            return 2;
        }
    const auto physicalPpi = android::screenDensity();
    readingPpi.store(physicalPpi);
    ui::setDensity(physicalPpi);
    QApplication::setFont(ui::interfaceFont());
    initializeDiagnostics(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/diagnostics");
    Window window(engineFontInputs(*assets, "/system/fonts", {}, {}), placeholderFontFamily(*assets, copies));
    window.showFullScreen();
    qInfo() << "Kobalt Android ready; density" << physicalPpi;
    return app.exec();
}
