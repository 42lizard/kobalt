#pragma once
#include <QByteArray>
#include <QString>
#include <QIODevice>
#include <memory>

namespace kobalt {
// Sequential stored/deflated ZIP input. The caller must already hold Nickel's
// accepted publication. This reads bytes only; it cannot decode protected data.
class ZipResource {
public:
    ZipResource();
    ~ZipResource();
    bool open(const QString &path, const QString &name, qint64 limit = 32 * 1024 * 1024);
    // The caller keeps source alive until the stream is destroyed.
    bool open(QIODevice &source, const QString &name, qint64 limit = 32 * 1024 * 1024);
    QByteArray next();
    qint64 size() const;
    bool valid() const;
    bool finished() const;

private:
    struct Data;
    std::unique_ptr<Data> data_;
};
}
