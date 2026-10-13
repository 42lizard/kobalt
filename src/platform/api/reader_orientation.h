#pragma once
#include <QJsonObject>
#ifndef KOBALT_NO_SENSORS
#include <QOrientationSensor>
#else
class QOrientationSensor;
#endif
#include <QPointer>
#include <QTimer>
#include <functional>

class QScreen;

namespace kobalt {
// The orientation that the screen shows now. Qt 6 reports Qt::PrimaryOrientation
// until the first orientation change, and Nickel makes none at boot. Turning to
// PrimaryOrientation selects the panel's own rotation, which is landscape on the
// Libra Colour. Resolve it from the screen's shape, as Qt 5 does. The result is
// PrimaryOrientation only without a screen.
Qt::ScreenOrientation shownOrientation(const QScreen *screen);

class ReaderOrientation : public QObject
#ifndef KOBALT_NO_SENSORS
    ,
                          private QOrientationFilter
#endif
{
public:
    // Ownership begins before the first turn and ends after restoring the native
    // orientation. Repeated start/stop calls do not repeat ownership callbacks.
    ReaderOrientation(QOrientationSensor *sensor, std::function<void(Qt::ScreenOrientation)> turn,
                      std::function<void(bool)> rotationOwnership = {});
    ~ReaderOrientation() override;
    void configure(const QJsonObject &settings);
    // stop() restores nativeOrientation, unless it is PrimaryOrientation.
    void start(Qt::ScreenOrientation nativeOrientation);
    void stop();
    void rotate();
    bool setAutomatic(bool enabled);
    void setBlocked(bool blocked);
    bool automaticAvailable() const {
#ifndef KOBALT_NO_SENSORS
        return !sensor.isNull();
#else
        return false;
#endif
    }
    bool automatic() const {
        return automaticRotation;
    }
    Qt::ScreenOrientation manual() const {
        return manualOrientation;
    }
    QJsonObject settings() const;

private:
    std::function<void(bool)> rotationOwnership;
#ifndef KOBALT_NO_SENSORS
    QPointer<QOrientationSensor> sensor;
#endif
    std::function<void(Qt::ScreenOrientation)> turn;
    QTimer debounce;
    Qt::ScreenOrientation manualOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation nativeOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation currentOrientation = Qt::PortraitOrientation;
    Qt::ScreenOrientation pending = Qt::PrimaryOrientation;
    bool active = false, blocked = false, automaticRotation = false, startedSensor = false;
#ifndef KOBALT_NO_SENSORS
    bool filter(QOrientationReading *reading) override;
#endif
    void deliver();
    void apply(Qt::ScreenOrientation value) {
        currentOrientation = value;
        turn(value);
    }
};
}
