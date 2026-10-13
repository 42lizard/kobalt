#include "support/qt_compat.h"
#include "reader_orientation.h"
#include <QScreen>

namespace kobalt {
namespace {
Qt::ScreenOrientation validOrientation(int value) {
    switch (value) {
    case Qt::PortraitOrientation:
    case Qt::LandscapeOrientation:
    case Qt::InvertedPortraitOrientation:
    case Qt::InvertedLandscapeOrientation:
        return static_cast<Qt::ScreenOrientation>(value);
    default:
        return Qt::PortraitOrientation;
    }
}
}

Qt::ScreenOrientation shownOrientation(const QScreen *screen) {
    if (!screen)
        return Qt::PrimaryOrientation;
    const auto orientation = screen->orientation();
    return orientation == Qt::PrimaryOrientation ? screen->primaryOrientation() : orientation;
}

ReaderOrientation::ReaderOrientation(QOrientationSensor *sensor, std::function<void(Qt::ScreenOrientation)> turn,
                                     std::function<void(bool)> rotationOwnership)
    : rotationOwnership(std::move(rotationOwnership)),
#ifndef KOBALT_NO_SENSORS
      sensor(sensor),
#endif
      turn(std::move(turn)) {
#ifdef KOBALT_NO_SENSORS
    Q_ASSERT(!sensor);
    Q_UNUSED(sensor);
#endif
    debounce.setSingleShot(true);
    debounce.setInterval(250);
    connect(&debounce, &QTimer::timeout, this, [this] {
        deliver();
    });
}
ReaderOrientation::~ReaderOrientation() {
    stop();
}
void ReaderOrientation::configure(const QJsonObject &settings) {
    manualOrientation = validOrientation(settings.value("orientation").toInt(Qt::PortraitOrientation));
    setAutomatic(settings.value("auto_rotate").toBool(false));
    if (active)
        apply(manualOrientation);
}
void ReaderOrientation::start(Qt::ScreenOrientation orientation) {
    if (active)
        return;
    nativeOrientation = orientation;
    active = true;
    if (rotationOwnership) {
        rotationOwnership(true);
    }
#ifndef KOBALT_NO_SENSORS
    if (sensor)
        sensor->addFilter(this);
#endif
    const bool automatic = automaticRotation;
    apply(manualOrientation);
    setAutomatic(automatic);
}
void ReaderOrientation::stop() {
    if (!active)
        return;
    active = false;
    debounce.stop();
    pending = Qt::PrimaryOrientation;
#ifndef KOBALT_NO_SENSORS
    if (sensor) {
        sensor->removeFilter(this);
        if (startedSensor)
            sensor->stop();
    }
    startedSensor = false;
#endif
    // PrimaryOrientation is not the orientation Nickel showed; see shownOrientation().
    if (nativeOrientation != Qt::PrimaryOrientation)
        apply(nativeOrientation);
    if (rotationOwnership) {
        rotationOwnership(false);
    }
}
void ReaderOrientation::rotate() {
    setAutomatic(false);
    manualOrientation = currentOrientation == Qt::InvertedLandscapeOrientation
                            ? Qt::PortraitOrientation
                            : static_cast<Qt::ScreenOrientation>(int(currentOrientation) * 2);
    if (active)
        apply(manualOrientation);
}
bool ReaderOrientation::setAutomatic(bool enabled) {
    debounce.stop();
    pending = Qt::PrimaryOrientation;
#ifndef KOBALT_NO_SENSORS
    automaticRotation = enabled && sensor;
    if (!automaticRotation && sensor && startedSensor) {
        sensor->stop();
        startedSensor = false;
    }
    if (active && automaticRotation) {
        if (!sensor->isActive()) {
            startedSensor = sensor->start();
            if (!startedSensor) {
                automaticRotation = false;
                return false;
            }
        }
        if (sensor->reading())
            filter(sensor->reading());
    }
#else
    automaticRotation = false;
#endif
    return !enabled || automaticRotation;
}
void ReaderOrientation::setBlocked(bool value) {
    blocked = value;
    if (!blocked && pending != Qt::PrimaryOrientation && !debounce.isActive())
        debounce.start(0);
}
#ifndef KOBALT_NO_SENSORS
bool ReaderOrientation::filter(QOrientationReading *reading) {
    if (!active)
        return true;
    if (automaticRotation) {
        Qt::ScreenOrientation next = Qt::PrimaryOrientation;
        switch (reading->orientation()) {
        case QOrientationReading::TopUp:
            next = Qt::PortraitOrientation;
            break;
        case QOrientationReading::TopDown:
            next = Qt::InvertedPortraitOrientation;
            break;
        case QOrientationReading::LeftUp:
            next = Qt::LandscapeOrientation;
            break;
        case QOrientationReading::RightUp:
            next = Qt::InvertedLandscapeOrientation;
            break;
        default:
            break;
        }
        if (next == Qt::PrimaryOrientation || next == currentOrientation) {
            pending = Qt::PrimaryOrientation;
            debounce.stop();
        } else if (next != pending) {
            // Repeated samples at the same angle must not postpone the turn.
            pending = next;
            debounce.start(250);
        }
    }
    // Nickel keeps its connection. It receives readings again after stop().
    return false;
}
#endif
void ReaderOrientation::deliver() {
    if (!active || !automaticRotation || blocked || pending == Qt::PrimaryOrientation)
        return;
    const auto orientation = pending;
    pending = Qt::PrimaryOrientation;
    apply(orientation);
}
QJsonObject ReaderOrientation::settings() const {
    return kobalt::qt::object({{"orientation", int(manualOrientation)}, {"auto_rotate", automaticRotation}});
}
}
