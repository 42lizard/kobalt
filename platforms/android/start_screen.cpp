#include "start_screen.h"
#include "interfaces/library/library_view.h"
#include "library/library_store.h"
#include "canvas.h"
#include "interfaces/reader/views/opening.h"
#include "support/strings.h"
#include <QDialog>
#include <QResizeEvent>

namespace kobalt::android {
StartScreen::StartScreen(PresentationFactory presentation, library::Store &store, QWidget *parent)
    : QStackedWidget(parent), presentation(std::move(presentation)), store(store) {
    setObjectName("androidStart");
    showLibrary();
}
bool StartScreen::dismissModal() {
    return libraryShown() && view->dismiss();
}
void StartScreen::buildLibrary() {
    if (view)
        return;
    // The library uses the reader's controls, which are sized in device
    // pixels. Its canvas maps them onto this logical-pixel window.
    canvas = new WidgetCanvas(this);
    canvas->setObjectName("androidLibraryCanvas");
    canvas->setStyleSheet("QGraphicsView{border:0;}");
    view = new library::LibraryView(store.index(), [this](const QString &name) {
        return store.coverPath(name);
    });
    // Complete the subtree before embedding it; proxy resize can disable new children.
    canvas->setContent(view);
    canvas->viewportResized = [this] {
        fitLibrary();
    };
    addWidget(canvas);
    view->openBook = [this](const QString &key) {
        emit bookRequested(key);
    };
    view->addFolder = [this] {
        emit addFolderRequested();
    };
    view->openFile = [this] {
        emit libraryOpenRequested();
    };
    view->rescan = [this] {
        store.scan();
    };
    view->removeFolder = [this](const QString &key) {
        emit removeFolderRequested(key);
    };
    view->removeBook = [this](const QString &key) {
        emit removeBookRequested(key);
    };
    view->markBook = [this](const QString &key, library::Book::Mark mark) {
        store.setMark(key, mark);
    };
    view->settingsChanged = [this](const library::Settings &settings) {
        store.setSettings(settings);
    };
    store.changed = [this] {
        view->setScanning(store.scanning(), store.found());
        view->refresh();
    };
    view->singleBooksAreCopies = singleBooksAreCopies;
    view->setDark(dark);
    view->show();
    if (hostScaled)
        setLibraryScale(scale.pixels, scale.scale, scale.nativePixels, scale.rasterScale);
}
library::LibraryView *StartScreen::library() {
    buildLibrary();
    return view;
}
WidgetCanvas *StartScreen::libraryCanvas() {
    buildLibrary();
    return canvas;
}
void StartScreen::showLibrary() {
    buildLibrary();
    setCurrentWidget(canvas);
    fitLibrary();
    view->setScanning(store.scanning(), store.found());
    view->refresh();
}
void StartScreen::showLibraryError(const QString &error) {
    showLibrary();
    view->showNotice(strings::get("library.open_failed"), error);
}
void StartScreen::setSingleBooksAreCopies(bool value) {
    singleBooksAreCopies = value;
    if (view)
        view->singleBooksAreCopies = value;
}
bool StartScreen::libraryShown() const {
    return canvas && currentWidget() == canvas;
}
void StartScreen::setDark(bool value) {
    dark = value;
    if (view)
        view->setDark(dark);
}
void StartScreen::fitLibrary() {
    if (canvas && !hostScaled)
        canvas->fitContent();
}
void StartScreen::setLibraryScale(QSize pixels, qreal factor, bool nativePixels, qreal rasterScale) {
    hostScaled = true;
    scale = {pixels, factor, rasterScale, nativePixels};
    if (!view)
        return;
    view->resize(pixels);
    canvas->setNativePixels(nativePixels);
    canvas->setRasterScale(rasterScale);
    canvas->setTransform(QTransform::fromScale(factor, factor));
    canvas->setSceneRect(QRectF(QPointF(), pixels));
}
void StartScreen::resizeEvent(QResizeEvent *event) {
    QStackedWidget::resizeEvent(event);
    fitLibrary();
}
StartScreen::~StartScreen() {
    store.changed = {};
}
void StartScreen::showOpening() {
    // Recreate the view to reset progress and the delayed hint for each book.
    if (opening)
        removeWidget(opening->window());
    opening = createEmbeddedOpening(this, presentation);
    connect(opening->window(), &QDialog::rejected, this, &StartScreen::exitRequested);
    addWidget(opening->window());
    setCurrentWidget(opening->window());
}
void StartScreen::setProgress(int percent) {
    if (opening && currentWidget() == opening->window())
        opening->setProgress(percent);
}
}
