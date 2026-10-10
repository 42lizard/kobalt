#pragma once
#include <QObject>
#include <QString>
#include <functional>

namespace kobalt::android {
// Install on Qt's GUI thread. Delivery stops when owner is destroyed.
void setCallbacks(QObject *owner, std::function<void(QString)> openBook, std::function<void(int)> turnPage);
void setPageButtonsAvailable(bool available);
void activity(const char *method);
bool readingGeometryReady();
int screenDensity();
struct Battery {
    int percent = -1;
    bool charging = false;
};
Battery battery();
void showKeyboard();
void hideKeyboard();
}
