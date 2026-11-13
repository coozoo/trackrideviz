#include "gzip.h"

#include <zlib.h>

namespace
{
// 15: the largest window; + 16: a gzip header and trailer instead of zlib's
const int kGzipWindow = 15 + 16;
const int kChunk = 64 * 1024;
}

bool Gzip::isGzip(const QByteArray &data)
{
    return data.size() >= 2 && quint8(data.at(0)) == 0x1f && quint8(data.at(1)) == 0x8b;
}

QByteArray Gzip::compress(const QByteArray &data)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, kGzipWindow, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        {
            return QByteArray();
        }
    QByteArray out;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    int result = Z_OK;
    do
        {
            char buffer[kChunk];
            stream.next_out = reinterpret_cast<Bytef *>(buffer);
            stream.avail_out = kChunk;
            result = deflate(&stream, Z_FINISH);
            out.append(buffer, kChunk - int(stream.avail_out));
        }
    while (result == Z_OK);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? out : QByteArray();
}

QByteArray Gzip::decompress(const QByteArray &data, bool *ok)
{
    if (ok)
        {
            *ok = false;
        }
    z_stream stream{};
    if (!isGzip(data) || inflateInit2(&stream, kGzipWindow) != Z_OK)
        {
            return QByteArray();
        }
    QByteArray out;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    int result = Z_OK;
    do
        {
            char buffer[kChunk];
            stream.next_out = reinterpret_cast<Bytef *>(buffer);
            stream.avail_out = kChunk;
            result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END)
                {
                    inflateEnd(&stream);
                    return QByteArray();
                }
            out.append(buffer, kChunk - int(stream.avail_out));
        }
    while (result != Z_STREAM_END);
    inflateEnd(&stream);
    if (ok)
        {
            *ok = true;
        }
    return out;
}
