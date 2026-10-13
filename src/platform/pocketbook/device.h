#pragma once
#include "platform/api/reader_host_services.h"

#include <QSize>

namespace kobalt::pocketbook {
QSize initialize();
QSize rotate(Qt::ScreenOrientation orientation);
void keepAwake(bool active);
ReaderHostServices readerServices();
}
