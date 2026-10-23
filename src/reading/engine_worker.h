#pragma once
class QObject;

namespace kobalt {
// Shared queue for reader sessions and their coordinate probes. Separate
// models must not access CREngine's global fonts and caches concurrently.
QObject *engineWorker();
}
