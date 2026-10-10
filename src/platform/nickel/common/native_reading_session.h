#pragma once
#include <QString>

namespace kobalt {
// Construct only after firmwareSupported() accepts Nickel, on the GUI thread.
class NativeReadingSession {
public:
    explicit NativeReadingSession(QString book);
    ~NativeReadingSession();
    static bool available();
    void start(double percent, const QString &position);
    void page(double percent, const QString &position);
    void stop();
#ifndef KOBALT_READER_HARDWARE
    static void testCommand();
#endif
private:
    QString book, account, position;
    double percent = 0;
    bool running = false;
};
}
