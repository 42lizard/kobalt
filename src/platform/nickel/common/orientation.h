#pragma once
#include <QOrientationSensor>

namespace kobalt {
// Firmware integrations must validate the native ABI before borrowing a sensor.
QOrientationSensor *nativeOrientationSensor();
void ownReaderRotation(bool owned);
void turnReaderScreen(Qt::ScreenOrientation orientation);
}
