#pragma once
// Expose only QString's legacy numeric overloads, retained by Qt 6.11.
#include <QtCore/qtcoreexports.h>
#undef QT_CORE_REMOVED_SINCE
#define QT_CORE_REMOVED_SINCE(major, minor) ((major) == 6 && (minor) == 9)
#include <QString>
#undef QT_CORE_REMOVED_SINCE
#define QT_CORE_REMOVED_SINCE(major, minor) 0
