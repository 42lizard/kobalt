#pragma once
#include <QJsonObject>
#include <QOrientationSensor>
#include <memory>

namespace kobalt::test {
// Only the separately compiled test runtime accepts this fixture. The normal
// desktop reader keeps unavailable hardware unavailable, even if the process
// environment contains the test option.
inline QString deviceFixture() {
#ifdef KOBALT_NATIVE_TEST_FIXTURES
    return qEnvironmentVariable("KOBALT_TEST_DEVICE_FIXTURE");
#else
    return {};
#endif
}
inline bool fixtureDeviceControls() {
    return deviceFixture() == "libra-colour";
}
inline std::unique_ptr<QOrientationSensor> fixtureOrientationSensor() {
    if (!fixtureDeviceControls())
        return {};
    auto sensor = std::make_unique<QOrientationSensor>();
    // An explicit nonexistent backend prevents accidental host sensor access.
    // This fixture exposes the Auto control for layout comparisons only.
    sensor->setIdentifier("kobalt-test-no-backend");
    return sensor;
}
inline QJsonObject fixtureProvenance() {
    return {{"name", deviceFixture().isEmpty() ? "none" : deviceFixture()},
            {"orientation_control", fixtureDeviceControls()},
            {"orientation_backend", false},
            {"orientation_readings", false},
            {"light_controls", fixtureDeviceControls()},
            {"light_backend", false},
            {"light_storage", "in-memory-only"},
            {"animation_control", fixtureDeviceControls()},
            {"animation_backend", false}};
}
}
