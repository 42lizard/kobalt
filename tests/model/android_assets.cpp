#include "assets/assets.h"
#include "platform/android/assets.h"
#include "check.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using kobalt::AssetBundle;
using kobalt::android::installAssets;

namespace {
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot open asset fixture");
    require(file.write(bytes) == bytes.size(), "Cannot write asset fixture");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath() + "/android-assets-XXXXXX");
    require(temporary.isValid(), "No temporary asset directory");
    require(!installAssets({}), "An empty data directory must not select the working directory");
    auto assets = installAssets(temporary.path());
    require(assets && assets->available(), "Bundled assets were not installed and verified");
    require(assets->fontData("test.ttf") == "synthetic font", "Installed asset bytes differ");
    assets = installAssets(temporary.path());
    require(bool(assets), "An existing installation did not verify");

    // Keep size and mtime unchanged to make another full hash observable.
    // The installed owner must retain its verification metadata, while the
    // deferred font reader still checks the bytes before returning them.
    const auto path = assets->file("fonts/test.ttf");
    const auto modified = QFileInfo(path).lastModified();
    write(path, "different font");
    QFile changed(path);
    require(changed.open(QIODevice::ReadWrite) && changed.setFileTime(modified, QFileDevice::FileModificationTime),
            "Cannot preserve the fixture timestamp");
    changed.close();
    require(assets->available(), "Android startup discarded the installed owner's verification cache");
    require(assets->fontData("test.ttf").isEmpty(), "Deferred reading accepted altered font bytes");
    require(!AssetBundle(assets->root(), QStringLiteral(KOBALT_VERSION)).available(),
            "A fresh owner did not detect the altered fixture");
    auto repaired = installAssets(temporary.path());
    require(repaired && repaired->fontData("test.ttf") == "synthetic font", "Reinstall did not repair damaged assets");

    write(path, "short");
    require(!repaired->available(), "Changed file metadata did not invalidate verification");
    repaired = installAssets(temporary.path());
    require(repaired && repaired->available(), "Changed-size assets were not repaired");
    write(temporary.path() + "/blocked", "not a directory");
    require(!installAssets(temporary.path() + "/blocked"), "Failed installation returned a usable owner");
}
