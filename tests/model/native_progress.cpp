#include "platforms/kobo/common/native_progress.h"
#include "check.h"
#include <QCoreApplication>

// Emit Nickel's signals without calling the PLT hooks. Calls inside Nickel
// can bypass those hooks, while Qt still delivers the connected signals.
class PlugWorkflowManager : public QObject {
    Q_OBJECT
public:
    static PlugWorkflowManager *sharedInstance();
signals:
    void aboutToConnect();
    void aboutToUnplug();
    void startedProcessing();
    void doneProcessing();
public slots:
    void unplugged() {
        emit aboutToUnplug();
    }
};
PlugWorkflowManager *PlugWorkflowManager::sharedInstance() {
    static PlugWorkflowManager manager;
    return &manager;
}

class PlugManager : public QObject {
    Q_OBJECT
public:
    static PlugManager *sharedInstance();
signals:
    void unplugged();
};
PlugManager *PlugManager::sharedInstance() {
    static PlugManager manager;
    return &manager;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto *usb = PlugWorkflowManager::sharedInstance();
    auto *plug = PlugManager::sharedInstance();
    QObject::connect(plug, &PlugManager::unplugged, usb, &PlugWorkflowManager::unplugged);
    kobalt::NativeProgress progress(QStringLiteral("cancel-unplug-fixture"));
    require(!progress.deviceTransition(), "reading starts outside a USB transition");
    progress.takeChange();

    for (int attempt = 0; attempt != 2; ++attempt) {
        // Cancel keeps the book open. Removing the charge-only cable then
        // runs Nickel's unplug slot, without a processing cycle or PLT hook.
        kobalt::NativeProgress::disconnected();
        require(!progress.deviceTransition(), "Cancel keeps page navigation available");
        progress.takeChange();
        emit plug->unplugged();
        require(progress.deviceTransition(), "unplug starts a progress transition");
        QCoreApplication::processEvents();
        require(!progress.deviceTransition(), "Cancel then unplug restores page navigation without a PLT hook");
        require(progress.takeChange(), "unplug makes native progress eligible for rechecking");
    }

    emit usb->aboutToConnect();
    require(progress.deviceTransition(), "confirmed connection blocks progress access");
    emit usb->startedProcessing();
    emit plug->unplugged();
    QCoreApplication::processEvents();
    kobalt::NativeProgress::disconnected();
    require(progress.deviceTransition(), "unplug cannot release progress during storage processing");
    emit usb->doneProcessing();
    require(!progress.deviceTransition(), "processing completion releases progress");
}

#include "native_progress.moc"
