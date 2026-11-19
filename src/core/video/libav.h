#pragma once

#include "mediainfo.h"

#include <QString>

// The program's own video engine: FFmpeg's libraries linked in (see libav.pri). Builds
// without them answer that it is not there, and everything goes through the ffmpeg program.
namespace Libav
{
// Built with the libraries.
bool available();
// "FFmpeg 8.1.2 (libavformat 62.12, libavcodec 62.28, libavfilter 11.14)"; empty without them.
QString version();

// What MediaProbe reads with ffprobe, read in-process: as fast as opening the file, and
// no ffprobe needed. False when the file cannot be read, with why in `error`, or when the
// program was built without the libraries.
bool probe(const QString &videoPath, MediaInfo *info, QString *error = nullptr);
}
