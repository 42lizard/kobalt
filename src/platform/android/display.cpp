#include "platform/api/kobo_refresh.h"
#include <cerrno>
namespace kobalt::platform {
// Android's Qt backing store coalesces ordinary widget updates. Kobo waveform
// commands have no Android equivalent; a device-specific adapter can add one.
void textRefresh(QWidget *) {}
void pageRefresh(QWidget *, bool, bool) {}
void setReaderRefreshWindow(QWidget *) {}
}
namespace kobalt::nickel4 {
// Android provides loopback. Never attempt Kobo's privileged interface repair.
bool configureLoopback(int *error) {
    if (error)
        *error = ENOTSUP;
    return false;
}
}
