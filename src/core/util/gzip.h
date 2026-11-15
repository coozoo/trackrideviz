#pragma once

#include <QByteArray>

// Standard gzip (RFC 1952), as gzip, zcat and every browser read it. Qt's
// own qCompress writes a variant of its own that those do not.
namespace Gzip
{
QByteArray compress(const QByteArray &data);
// *ok false when `data` is no gzip stream or a broken one, or would unpack to more than
// `maxSize` bytes (a small file can unpack to gigabytes: it is refused before that).
QByteArray decompress(const QByteArray &data, bool *ok = nullptr, qint64 maxSize = 512 * 1024 * 1024);
// Starts the way every gzip stream does.
bool isGzip(const QByteArray &data);
}
