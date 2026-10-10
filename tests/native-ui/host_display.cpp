#include "platform/api/kobo_refresh.h"
#include <QWidget>

namespace kobalt::platform {
// ReadingPage already combines body, header and footer damage into one update.
// Qt coalesces update requests before flushing its native backing store. Do
// not add an invalidate per child widget or a second full-window repaint here.
void textRefresh(QWidget *) {}
void pageRefresh(QWidget *, bool, bool) {}
void setReaderRefreshWindow(QWidget *) {}
}
