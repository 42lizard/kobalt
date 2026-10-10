#include "assets/font_catalog.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace kobalt;
static void expect(bool value, const char *message) {
    if (!value)
        qFatal("%s", message);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    expect(argc == 2, "supply a bundled font fixture");
    QTemporaryDir temporary;
    expect(temporary.isValid(), "temporary directory");
    const auto root = temporary.path();
    for (const auto &directory : {"ebook-core", "system", "sideload"}) {
        expect(QDir().mkpath(root + '/' + directory), "font source directory");
        expect(QFile::copy(QString::fromLocal8Bit(argv[1]), root + '/' + directory + "/Regular.ttf"),
               "copy bundled font fixture");
    }
    const auto previous = QDir::currentPath();
    expect(QDir::setCurrent(root + "/system"), "select populated working directory");
    expect(discoverReadingFonts(FontSources{}).isEmpty(), "empty font sources must not scan working directory");
    expect(QDir::setCurrent(previous), "restore working directory");
    FontSources sources{{root + "/ebook-core/Regular.ttf"}, root + "/system", root + "/sideload", root};
    const auto catalog = discoverReadingFonts(sources);
    expect(catalog.size() == 1, "duplicate families respect source precedence");
    expect(catalog.first().origin == FontOrigin::Recommended, "bundled reading font takes precedence");
    expect(catalog.first().files == sources.bundled, "lower-priority files do not mix with bundled family");
    expect(catalog.first().copyForQt, "removable bundled font requires byte registration");
    sources.bundled.clear();
    sources.removableRoot = root + "/sideload";
    auto system = discoverReadingFonts(sources);
    expect(system.size() == 1 && system.first().origin == FontOrigin::BuiltIn,
           "explicit system source takes precedence over sideload");
    expect(!system.first().copyForQt, "persistent system font need not copy");
    sources.systemDirectory.clear();
    auto sideload = discoverReadingFonts(sources);
    expect(sideload.size() == 1 && sideload.first().origin == FontOrigin::Sideloaded,
           "disabled system source leaves sideload");
    expect(sideload.first().copyForQt, "removable sideload font requires byte registration");
}
