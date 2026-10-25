#pragma once
#include <QByteArray>
#include <QVector>
#include <QtEndian>
#include <zlib.h>

namespace kobalt::test {
inline QByteArray dictzip(const QByteArray &body, int chunk = 24) {
    auto require = [](bool ok, const char *message) {
        if (!ok)
            qFatal("%s", message);
    };
    auto le16 = [](QByteArray &b, quint16 value) {
        value = qToLittleEndian(value);
        b.append(reinterpret_cast<const char *>(&value), 2);
    };
    auto le32 = [](QByteArray &b, quint32 value) {
        value = qToLittleEndian(value);
        b.append(reinterpret_cast<const char *>(&value), 4);
    };
    QVector<QByteArray> blocks;
    z_stream stream{};
    require(deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "deflate init");
    for (int at = 0; at < body.size(); at += chunk) {
        QByteArray out(compressBound(chunk) + 32, '\0');
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(body.constData() + at));
        stream.avail_in = qMin(chunk, int(body.size()) - at);
        stream.next_out = reinterpret_cast<Bytef *>(out.data());
        stream.avail_out = out.size();
        require(deflate(&stream, Z_FULL_FLUSH) == Z_OK, "deflate chunk");
        out.resize(out.size() - stream.avail_out);
        blocks.append(out);
    }
    QByteArray tail(64, '\0');
    stream.next_in = nullptr;
    stream.avail_in = 0;
    stream.next_out = reinterpret_cast<Bytef *>(tail.data());
    stream.avail_out = tail.size();
    require(deflate(&stream, Z_FINISH) == Z_STREAM_END, "deflate finish");
    tail.resize(tail.size() - stream.avail_out);
    deflateEnd(&stream);
    QByteArray extra("RA");
    le16(extra, 6 + blocks.size() * 2);
    le16(extra, 1);
    le16(extra, chunk);
    le16(extra, blocks.size());
    for (const auto &block : blocks)
        le16(extra, block.size());
    QByteArray result = QByteArray::fromHex("1f8b0804000000000003");
    le16(result, extra.size());
    result += extra;
    for (const auto &block : blocks) {
        result += block;
    }
    result += tail;
    le32(result, crc32(0, reinterpret_cast<const Bytef *>(body.constData()), body.size()));
    le32(result, body.size());
    return result;
}
}
