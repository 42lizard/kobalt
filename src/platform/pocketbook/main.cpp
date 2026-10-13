#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "device.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "support/diagnostics.h"
#include "window.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char **argv) {
    using namespace kobalt;
#ifndef KOBALT_POCKETBOOK_HOST
    qputenv("QT_PLUGIN_PATH", "/ebrmain/plugins");
    qputenv("QT_QPA_PLATFORM", "pocketbook2");
    QCoreApplication::setSetuidAllowed(true);
#endif
    const auto size = pocketbook::initialize();
    FontCopies copies;
    QApplication app(argc, argv);
    app.setApplicationName("Kobalt");
    app.setOrganizationName("Kobalt");
    app.setStyle("Fusion");
#ifdef KOBALT_POCKETBOOK_HOST
    const auto root =
        qEnvironmentVariable("KOBALT_DATA_ROOT", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const auto assetRoot = qEnvironmentVariable("KOBALT_ASSET_ROOT");
#else
    const QString root = "/mnt/ext1/system/kobalt";
    const QString assetRoot = root + "/assets";
#endif
    AssetBundle assets(assetRoot, QStringLiteral(KOBALT_VERSION));
    if (!assets.available() || !ui::initializeInterfaceFonts(assets)) {
        qCritical("Kobalt assets unavailable: %s", qPrintable(assetRoot));
        return 2;
    }
    for (const auto &font : bundledFontFiles(assets)) {
        if (QFontDatabase::addApplicationFont(font) < 0) {
            return 2;
        }
    }
    app.setFont(ui::interfaceFont());
    initializeDiagnostics(root + "/diagnostics");
    pocketbook::Window window(storage::Paths{root + "/config"}, engineFontInputs(assets, {}, root + "/fonts", {}),
                              placeholderFontFamily(assets, copies));
    window.resize(size);
    window.show();
    if (app.arguments().size() > 1) {
        QTimer::singleShot(0, &window, [&] {
            window.openBook(app.arguments().at(1));
        });
    }
    return app.exec();
}
