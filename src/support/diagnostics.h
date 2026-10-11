#pragma once
#include "qt_compat.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace kobalt {
QJsonObject diagnosticDeviceFields(const QByteArray &version);
QString diagnosticErrorCode(const QString &reason);
void pauseDiagnostics(bool paused);
void initializeDiagnostics(const QString &directory);
void diagnostic(const char *event, QJsonObject fields = {}, bool durable = true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
// Qt 5.2 cannot build a QJsonObject from a braced list of fields.
inline void diagnostic(const char *event, std::initializer_list<qt::JsonField> fields, bool durable = true) {
    diagnostic(event, qt::object(fields), durable);
}
#endif
}
