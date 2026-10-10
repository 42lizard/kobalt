#include "runtime.h"
#include <QGuiApplication>
#include <QInputMethod>
#include <QJniObject>
#include <QPointer>
#include <atomic>
#include <QtCore/qnativeinterface.h>
#include <utility>

namespace {
QPointer<QObject> owner;
std::function<void(QString)> openExternal;
std::function<void(int)> volumeButton;
std::atomic<bool> pageButtonsAvailable{false};
}
namespace kobalt::android {
void setCallbacks(QObject *receiver, std::function<void(QString)> openBook, std::function<void(int)> turnPage) {
    pageButtonsAvailable.store(false);
    owner = receiver;
    openExternal = std::move(openBook);
    volumeButton = std::move(turnPage);
}
void setPageButtonsAvailable(bool available) {
    pageButtonsAvailable.store(available);
}
void activity(const char *method) {
    const QByteArray name(method);
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([name] {
        QNativeInterface::QAndroidApplication::context().callMethod<void>(name.constData());
    });
}
bool readingGeometryReady() {
    return QNativeInterface::QAndroidApplication::context().callMethod<jboolean>("readerGeometryReady");
}
int screenDensity() {
    return qBound(100, int(QNativeInterface::QAndroidApplication::context().callMethod<jint>("readerDensity")), 600);
}
Battery battery() {
    const int value = QNativeInterface::QAndroidApplication::context().callMethod<jint>("readerBattery");
    return {value < 0 ? -1 : value % 256, value >= 256};
}
void showKeyboard() {
    QGuiApplication::inputMethod()->show();
}
void hideKeyboard() {
    QGuiApplication::inputMethod()->hide();
}
}
extern "C" JNIEXPORT void JNICALL Java_org_kobalt_android_KobaltActivity_openBook(JNIEnv *env, jobject, jstring value) {
    const auto text = env->GetStringUTFChars(value, nullptr);
    if (!text)
        return;
    const auto path = QString::fromUtf8(text);
    env->ReleaseStringUTFChars(value, text);
    QMetaObject::invokeMethod(qApp, [path] {
        if (owner && openExternal)
            openExternal(path);
    });
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_kobalt_android_KobaltActivity_turnPage(JNIEnv *, jobject,
                                                                                      jint direction) {
    // Android's UI thread must not wait for Qt while Qt can request Android UI work.
    if (!pageButtonsAvailable.load())
        return false;
    QMetaObject::invokeMethod(qApp, [direction] {
        if (owner && volumeButton)
            volumeButton(direction);
    });
    return true;
}
