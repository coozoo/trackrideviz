#pragma once

#include "mediainfo.h"

#include <QString>
#include <QStringList>

// The program's own video engine: FFmpeg's libraries linked in (see libav.pri). Builds
// without them answer that it is not there, and everything goes through the ffmpeg program.
namespace Libav
{
// Built with the libraries.
bool available();
// The libraries' messages through Qt's (category rtv.libav), errors only; libva's errors only.
// Done by every use here; main() calls it early, before anything opens the graphics card.
void quiet();
// "FFmpeg 8.1.2 (libavformat 62.12, libavcodec 62.28, libavfilter 11.14)"; empty without them.
QString version();

// What MediaProbe reads with ffprobe, read in-process: as fast as opening the file, and
// no ffprobe needed. False when the file cannot be read, with why in `error`, or when the
// program was built without the libraries.
bool probe(const QString &videoPath, MediaInfo *info, QString *error = nullptr);

// The encoder is in the libraries.
bool hasEncoder(const QString &id);
// A tiny test encode with it succeeds, with `options` (ffmpeg's flags, "-c:v" left out) and
// on the VA-API device `vaapiDevice` when not empty: what tells whether a graphics card's
// encoder really works here. Blocking, a fraction of a second; call from a worker thread.
bool encoderWorks(const QString &id, const QStringList &options, const QString &vaapiDevice = QString());
}
