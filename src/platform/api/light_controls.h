#pragma once
#include <QTime>

namespace kobalt {
// One window owns one light controller. A cancelled gesture restores its
// original value; a committed gesture persists through the host settings.
class LightControls {
public:
    virtual ~LightControls() = default;
    virtual bool initialize() = 0;
    virtual bool hasWarmth() const = 0;
    virtual int warmth() const = 0;
    virtual int warmthMinimum() const = 0;
    virtual int warmthMaximum() const = 0;
    virtual int warmthStep() const = 0;
    virtual bool automaticWarmth() const = 0;
    virtual int setWarmth(int value) = 0;
    virtual bool saveWarmth() = 0;
    virtual bool setAutomaticWarmth(bool enabled) = 0;
    virtual bool beginGesture() = 0;
    virtual int setGestureLevel(int percent) = 0;
    virtual void finishGesture(bool save) = 0;
    virtual int brightness() const = 0;
    virtual bool gesturing() const = 0;
};
}
