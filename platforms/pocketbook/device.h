#pragma once
#include "platform/api/reader_host_services.h"

#include <QSize>

namespace kobalt::pocketbook {
QSize initialize();
QSize rotate(Qt::ScreenOrientation orientation);
void keepAwake(bool active);
void bookReady(const QString &path, const QString &title);
bool openNativeBook(const QString &path);
ReaderHostServices readerServices();
}
