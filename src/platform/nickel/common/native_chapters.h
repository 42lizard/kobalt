#pragma once
#include "reading/reading_bridge.h"
#include <QObject>
#include <QVector>
#include <QString>
#include <functional>
#include <memory>

namespace kobalt {
using NativeChapterData = ReadingChapterData;

// Counts with Nickel's authorized parser on an owned worker. Destruction
// cancels the request; drain must finish before exporting or closing storage.
class NativeChapters : public QObject, public ReadingChapters {
public:
    explicit NativeChapters(QString identity);
    ~NativeChapters() override;
    const NativeChapterData &data() const override {
        return result;
    }
    static void drain(std::function<void()> finished);
#ifndef KOBALT_READER_HARDWARE
    static void testCommand();
#endif
private:
    struct Job;
    std::shared_ptr<Job> job;
    NativeChapterData result;
};
}
