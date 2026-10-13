#pragma once

#include <QApplication>
#include <QEvent>

namespace kobalt::pocketbook {
class Application final : public QApplication {
public:
    using QApplication::QApplication;

protected:
    bool compressEvent(QEvent *event, QObject *receiver, QPostEventList *events) override {
        // PocketBook's QPA posts plain UpdateLater events to its QObject.
        // QWidget compression assumes these contain a QRegion and crashes.
        if (event->type() == QEvent::UpdateLater && !receiver->isWidgetType()) {
            return QGuiApplication::compressEvent(event, receiver, events);
        }
        return QApplication::compressEvent(event, receiver, events);
    }
};
}
