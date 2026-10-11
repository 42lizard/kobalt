#include "publication/zip_resource.h"
#include "support/qt_compat.h"
#include "publication/resource_cache.h"
#include <QCoreApplication>
#include <QFile>
#include <QBuffer>
#include <QTemporaryDir>
#include <QtEndian>
#include <zlib.h>
using namespace kobalt;
namespace {
void u16(QByteArray &b, quint16 v) {
    v = qToLittleEndian(v);
    b.append(reinterpret_cast<char *>(&v), 2);
}
void u32(QByteArray &b, quint32 v) {
    v = qToLittleEndian(v);
    b.append(reinterpret_cast<char *>(&v), 4);
}
void replace16(QByteArray &bytes, int offset, quint16 value) {
    QByteArray encoded;
    u16(encoded, value);
    bytes.replace(offset, encoded.size(), encoded);
}
void replace32(QByteArray &bytes, int offset, quint32 value) {
    QByteArray encoded;
    u32(encoded, value);
    bytes.replace(offset, encoded.size(), encoded);
}
QByteArray zip(const QByteArray &source, bool deflated, bool badCrc = false, bool truncated = false) {
    QByteArray packed = source;
    if (deflated) {
        z_stream stream{};
        if (deflateInit2(&stream, 1, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            qFatal("deflate init");
        packed.resize(compressBound(source.size()));
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(source.constData()));
        stream.avail_in = source.size();
        stream.next_out = reinterpret_cast<Bytef *>(packed.data());
        stream.avail_out = packed.size();
        if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
            qFatal("deflate");
        packed.resize(stream.total_out);
        deflateEnd(&stream);
    }
    if (truncated)
        packed.chop(1);
    const quint32 crc =
        crc32(0, reinterpret_cast<const Bytef *>(source.constData()), source.size()) ^ (badCrc ? 1U : 0U);
    const QByteArray name("image.png");
    QByteArray bytes, directory;
    u32(bytes, 0x04034b50);
    u16(bytes, 20);
    u16(bytes, 0);
    u16(bytes, deflated ? 8 : 0);
    u32(bytes, 0);
    u32(bytes, crc);
    u32(bytes, packed.size());
    u32(bytes, source.size());
    u16(bytes, name.size());
    u16(bytes, 0);
    bytes += name;
    bytes += packed;
    const auto offset = bytes.size();
    u32(directory, 0x02014b50);
    u16(directory, 20);
    u16(directory, 20);
    u16(directory, 0);
    u16(directory, deflated ? 8 : 0);
    u32(directory, 0);
    u32(directory, crc);
    u32(directory, packed.size());
    u32(directory, source.size());
    u16(directory, name.size());
    u16(directory, 0);
    u16(directory, 0);
    u16(directory, 0);
    u16(directory, 0);
    u32(directory, 0);
    u32(directory, 0);
    directory += name;
    bytes += directory;
    u32(bytes, 0x06054b50);
    u16(bytes, 0);
    u16(bytes, 0);
    u16(bytes, 1);
    u16(bytes, 1);
    u32(bytes, directory.size());
    u32(bytes, offset);
    u16(bytes, 0);
    return bytes;
}
void save(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        qFatal("write");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid())
        qFatal("temporary directory");
    const auto path = dir.path() + "/book.zip";
    for (bool deflated : {false, true})
        for (int length : {0, 1, 65535, 65536, 65537, 196608, 2097171}) {
            QByteArray original(length, 'a');
            quint32 state = 913;
            for (int i = 0; i < length; ++i) {
                state = state * 1664525U + 1013904223U;
                original[i] = char(state >> 24);
            }
            // The second pattern covers very small compressed input producing many blocks.
            for (bool compressible : {false, true}) {
                const auto input = compressible ? QByteArray(length, 'a') : original;
                save(path, zip(input, deflated));
                ZipResource first, second;
                if (!first.open(path, "image.png") || !second.open(path, "image.png") || first.size() != length)
                    qFatal("open");
                QByteArray a, b;
                while (!first.finished() || !second.finished()) {
                    auto x = first.next(), y = second.next();
                    if (!first.valid() || !second.valid() || x.size() > ResourceCache::blockBytes ||
                        y.size() > ResourceCache::blockBytes)
                        qFatal("bounded read");
                    a += x;
                    b += y;
                }
                if (a != input || b != input)
                    qFatal("bytes or concurrent positions");
            }
            for (bool truncated : {false, true}) {
                if (!length && truncated)
                    continue;
                save(path, zip(original, deflated, !truncated, truncated));
                ZipResource stream;
                if (stream.open(path, "image.png")) {
                    qint64 received = 0;
                    while (stream.valid() && !stream.finished())
                        received += stream.next().size();
                    if (stream.valid() || (length && received >= length))
                        qFatal("corrupt entry accepted as complete");
                }
            }
        }
    for (bool deflated : {false, true}) {
        QBuffer borrowed;
        borrowed.setData(zip("borrowed payload", deflated));
        if (!borrowed.open(QIODevice::ReadOnly))
            qFatal("open borrowed input");
        {
            ZipResource stream;
            if (!stream.open(borrowed, "image.png"))
                qFatal("open borrowed ZIP resource");
            QByteArray bytes;
            while (stream.valid() && !stream.finished())
                bytes += stream.next();
            if (!stream.valid() || bytes != "borrowed payload")
                qFatal("borrowed resource bytes");
        }
        if (!borrowed.isOpen() || !borrowed.seek(0) || borrowed.read(4) != QByteArray("PK\3\4", 4))
            qFatal("resource destroyed or closed borrowed input");
        borrowed.close();
        borrowed.setData(zip("corrupt", deflated, true));
        if (!borrowed.open(QIODevice::ReadOnly))
            qFatal("reopen borrowed input");
        ZipResource corrupt;
        if (!corrupt.open(borrowed, "image.png"))
            qFatal("open borrowed corrupt resource");
        while (corrupt.valid() && !corrupt.finished())
            corrupt.next();
        if (corrupt.valid())
            qFatal("borrowed input bypassed CRC validation");
    }
    save(path, zip("payload", false));
    ZipResource stream;
    if (stream.open(path, "absent.png"))
        qFatal("missing entry accepted");
    {
        const auto original = zip("payload", false);
        const int directory = original.size() - 22 - 46 - QByteArray("image.png").size();
        auto reject = [&](QByteArray bytes, const char *reason) {
            save(path, original);
            if (!stream.open(path, "image.png"))
                qFatal("valid stream did not open before rejection");
            save(path, bytes);
            if (stream.open(path, "image.png"))
                qFatal("%s", reason);
            if (stream.valid() || stream.finished() || stream.size() != -1 || !stream.next().isEmpty())
                qFatal("Failed reopen retained the previous resource");
        };
        auto changed = original;
        replace16(changed, 6, 8);
        reject(changed, "Local and central flags disagree");
        changed = original;
        replace16(changed, 8, 8);
        reject(changed, "Local and central compression methods disagree");
        changed = original;
        changed[30] = 'X';
        reject(changed, "Local and central member names disagree");
        changed = original;
        replace16(changed, directory + 10, 99);
        reject(changed, "Unsupported compression method accepted");
        changed = original;
        replace16(changed, directory + 8, 1);
        reject(changed, "Encrypted member accepted");
        changed = original;
        replace16(changed, directory + 34, 1);
        reject(changed, "Member from another disk accepted");
        changed = original;
        replace32(changed, directory + 42, quint32(directory));
        reject(changed, "Local header overlaps the central directory");
        changed = original;
        replace16(changed, 28, quint16(directory));
        reject(changed, "Local extra field moves payload into the directory");
        changed = original;
        replace16(changed, directory + 28, 65535);
        reject(changed, "Member name extends past the central directory");
        changed = original;
        replace32(changed, original.size() - 22 + 16, quint32(directory + 1));
        reject(changed, "Central directory does not end at the trailer");
        save(path, original);
        for (qint64 limit : {qint64(-1), qint64(6)}) {
            if (stream.open(path, "image.png", limit))
                qFatal("Invalid or undersized resource limit accepted");
        }
        if (!stream.open(path, "image.png", 7) || stream.next() != "payload" || !stream.finished() || !stream.valid())
            qFatal("Resource at its exact limit rejected");
        save(path, zip({}, false));
        if (!stream.open(path, "image.png", 0) || !stream.next().isEmpty() || !stream.finished() || !stream.valid())
            qFatal("Empty resource at a zero limit rejected");
    }
    qInfo("PASS: stored, deflated, boundaries, independent readers, CRC, truncation, header agreement and limits");
}
