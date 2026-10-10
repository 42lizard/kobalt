#pragma once
#include <QApplication>
#include <QFontInfo>
#include <QFontMetrics>
#include <QJsonObject>

namespace kobalt::test {
inline QByteArray platformRequest;
inline QByteArray selectedHostPlatform(const QByteArray &override, bool background) {
    if (!override.isEmpty())
        return override;
#ifdef Q_OS_MACOS
    return background ? "minimal:enable_fonts:freetype" : "cocoa:fontengine=freetype";
#else
    return background ? "offscreen" : QByteArray();
#endif
}
inline void configureHostPlatform(int argc, char **argv, bool background) {
    platformRequest = qgetenv("QT_QPA_PLATFORM");
    for (int i = 1; i + 1 < argc; ++i) {
        if (QByteArray(argv[i]) == "-platform") {
            platformRequest = argv[i + 1];
            return; // Qt's explicit command-line option takes precedence.
        }
    }
    platformRequest = selectedHostPlatform(platformRequest, background);
    if (!platformRequest.isEmpty())
        qputenv("QT_QPA_PLATFORM", platformRequest);
}
inline QJsonObject hostFontProvenance() {
    QFont font("Libron");
    font.setPixelSize(32);
    const QFontMetrics metrics(font);
    // Qt has no public font-engine-name query. Record the requested plugin
    // options and actual metrics, rather than claiming a backend from a name.
    return {{"platform_request", QString::fromLatin1(platformRequest)},
            {"platform_plugin", QGuiApplication::platformName()},
            {"probe_family", QFontInfo(font).family()},
            {"probe_pixel_size", 32},
            {"probe_ascent", metrics.ascent()},
            {"probe_descent", metrics.descent()},
            {"probe_height", metrics.height()}};
}
}
