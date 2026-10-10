#include "interfaces/reader/window/font_registration.h"
#include "check.h"

#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 2, "Expected a loadable font fixture");
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create font-registration directory");
    const QString path = directory.path() + "/retry.ttf";
    for (bool copyForQt : {false, true}) {
        QSet<QString> paths;
        QVector<int> ids;
        kobalt::reader::registerPickerFont(path, copyForQt, paths, ids);
        require(paths.isEmpty() && ids.isEmpty(), "A failed font registration blocked retries");
        QFile damaged(path);
        require(damaged.open(QIODevice::WriteOnly) && damaged.write("not a font") == 10,
                "Cannot stage malformed font fixture");
        damaged.close();
        kobalt::reader::registerPickerFont(path, copyForQt, paths, ids);
        require(paths.isEmpty() && ids.isEmpty(), "A malformed readable font blocked retries");
        require(QFile::remove(path) && QFile::copy(QString::fromLocal8Bit(argv[1]), path),
                "Cannot stage loadable font fixture");
        kobalt::reader::registerPickerFont(path, copyForQt, paths, ids);
        require(paths.contains(path) && ids.size() == 1 &&
                    !QFontDatabase::applicationFontFamilies(ids.first()).isEmpty(),
                "A loadable font did not register on retry");
        kobalt::reader::registerPickerFont(path, copyForQt, paths, ids);
        require(ids.size() == 1, "Registered font was loaded twice");
        QFontDatabase::removeApplicationFont(ids.first());
        require(QFile::remove(path), "Cannot remove font fixture before the next mode");
    }
    std::puts("PASS: failed font registration retries once the same path becomes loadable");
}
