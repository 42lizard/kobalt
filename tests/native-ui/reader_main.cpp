#include "reader_host.h"
#include "host_style.h"
#include "host_platform.h"
#include "reader_canvas.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "support/diagnostics.h"
#include "storage/reader_storage.h"
#include "interfaces/reader/ui/style/fonts.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QDebug>
#include <QTimer>
#include <QFontInfo>
#include <QCommandLineParser>

using namespace kobalt;
class HostWindow : public test::ReaderCanvas {
public:
    HostWindow(QWidget *reader, QSize size, test::ReaderHost &host) : ReaderCanvas(reader, size), host(host) {}

protected:
    void closeEvent(QCloseEvent *event) override {
        event->ignore();
        host.shutdown();
    }

private:
    test::ReaderHost &host;
};
int main(int argc, char **argv) {
    test::configureHostPlatform(argc, argv, false);
    FontCopies fontCopies;
    QApplication app(argc, argv);
    test::applyHostStyle();
    app.setApplicationName("Kobalt Native Reader");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("epub", "A DRM-free EPUB path");
    parser.addOption({"size", "Native reader dimensions", "WIDTHxHEIGHT", "1072x1448"});
    parser.process(app);
    const auto dimensions = parser.value("size").split('x');
    const QSize nativeSize(dimensions.value(0).toInt(), dimensions.value(1).toInt());
    if (parser.positionalArguments().size() != 1 || nativeSize.width() < 320 || nativeSize.height() < 320 ||
        nativeSize.width() > 4096 || nativeSize.height() > 4096) {
        qCritical() << "Supply one EPUB and --size WIDTHxHEIGHT between 320 and 4096.";
        return 2;
    }
    AssetBundle assets(QStringLiteral(KOBALT_ASSET_ROOT), QStringLiteral(KOBALT_VERSION));
    if (!assets.available() || !ui::initializeInterfaceFonts(assets))
        return 1;
    const auto files = bundledFontFiles(assets);
    if (files.isEmpty()) {
        qCritical() << "No bundled fonts available.";
        return 1;
    }
    for (const auto &file : files)
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical() << "Cannot register bundled font" << file;
            return 1;
        }
    if (!QFontInfo(ui::interfaceFont()).family().startsWith("Kobalt UI Sans") ||
        !QFontInfo(ui::captionFont()).family().startsWith("Kobalt UI Mono") ||
        !QFontInfo(ui::brandFont(30)).family().startsWith("Kobalt UI Serif")) {
        qCritical() << "The interface substituted a bundled font.";
        return 1;
    }
    ui::setDensity(300);
    readingPpi.store(300);
    QApplication::setFont(ui::interfaceFont());
    qInfo() << "Native font profile" << test::hostFontProvenance();
    storage::Paths paths{QStringLiteral(KOBALT_STORAGE_ROOT)};
    paths.bookPrefix = "book-";
    paths.active = paths.directory + "/active-session.json";
    paths.stateReport = paths.directory + "/state.json";
    paths.sessionReport = paths.directory + "/session.json";
    initializeDiagnostics(paths.directory);
    test::ReaderHost host(paths, engineFontInputs(assets, {}, {}, {}), placeholderFontFamily(assets, fontCopies));
    std::unique_ptr<HostWindow> window;
    int result = 0;
    QObject::connect(&host, &test::ReaderHost::failed, &app, [&](const QString &error) {
        qCritical().noquote() << error;
        result = 1;
        if (host.idle())
            app.exit(result);
    });
    QObject::connect(&host, &test::ReaderHost::closed, &app, [&] {
        window.reset();
        app.exit(result);
    });
    QObject::connect(&host, &test::ReaderHost::opened, &app, [&] {
        window = std::make_unique<HostWindow>(host.window(), nativeSize, host);
        window->setWindowTitle("Kobalt · Native EPUB reader");
        window->resize(900, 900);
        window->show();
        // A device presentation adapter pauses other top-level windows. The
        // host owns this enclosing window, so it keeps painting either way.
        window->setUpdatesEnabled(true);
    });
    QTimer::singleShot(0, &host, [&] {
        host.open(parser.positionalArguments().first(), nativeSize);
    });
    return app.exec();
}
