#pragma once
#include <QByteArray>
#include <QWidget>
#include <QVariant>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>

namespace kobalt {
enum class RefreshBackend { Unknown, Imx, Mtk };
inline RefreshBackend refreshBackend(QWidget *reader) {
    // Native scope tests provide a backend without opening a framebuffer.
    const auto selected = reader ? reader->property("reader_refresh_backend").toByteArray() : QByteArray();
    static const QByteArray detected = [] {
        QByteArray driver;
        const int fd = ::open("/dev/fb0", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            fb_fix_screeninfo info{};
            if (::ioctl(fd, FBIOGET_FSCREENINFO, &info) == 0)
                driver = QByteArray(info.id, int(strnlen(info.id, sizeof(info.id))));
            ::close(fd);
        }
        return driver;
    }();
    const auto driver = selected.isEmpty() ? detected : selected;
    if (driver == "hwtcon")
        return RefreshBackend::Mtk;
    if (driver.startsWith("mxc_epdc"))
        return RefreshBackend::Imx;
    return RefreshBackend::Unknown;
}
}
