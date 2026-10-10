#pragma once
class QWidget;

// Internal contract between Nickel's hook entry point and its reader controller.
// Other application targets do not include this header.
namespace kobalt::nickel4 {
bool firmwareSupported();
int initializeReader();
#ifdef KOBALT_READER_HARDWARE
bool uninstallReader();
#endif
extern void (*nativeShowReader)(void *, const void *, const void *, void *, const void *);
extern void (*nativeFinishInitialization)(void *);
extern void (*nativeContinueUsing)(void *), (*nativeUnplugged)(void *);
}
