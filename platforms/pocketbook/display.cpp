#include "platform/api/kobo_refresh.h"
#include <QTimer>
#include <QWidget>
#include <cerrno>
#ifndef KOBALT_POCKETBOOK_HOST
#include <inkview.h>
#endif

namespace kobalt::platform {
// The PocketBook QPA owns paint/partial refresh. Request a clean update after
// the queued Qt paint for the reader's explicit clean-refresh boundary.
void textRefresh(QWidget *) {}
void pageRefresh(QWidget *widget, bool clean, bool) {
    if (!widget) {
        return;
    }
#ifndef KOBALT_POCKETBOOK_HOST
    if (clean) {
        QTimer::singleShot(200, widget, [] {
            FullUpdate();
        });
    }
#else
    Q_UNUSED(clean);
#endif
}
void setReaderRefreshWindow(QWidget *) {}
}
namespace kobalt::nickel4 {
bool configureLoopback(int *error) {
    if (error) {
        *error = ENOTSUP;
    }
    return false;
}
}
