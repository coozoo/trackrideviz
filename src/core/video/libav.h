#pragma once

#include "mediainfo.h"

#include <QString>
#include <QStringList>

#include <functional>

// The program's own video engine: FFmpeg's libraries linked in (see libav.pri). Builds
// without them answer that it is not there, and everything goes through the ffmpeg program.
namespace Libav
{
// Built with the libraries.
bool available();
// The libraries' messages through Qt's (category rtv.libav), errors only; libva's errors only.
// Done by every use here; main() calls it early, before anything opens the graphics card.
void quiet();
// On Android: the libraries given Java, which the phone's encoders and decoders (MediaCodec) and
// files by address (content://) need. main() calls it once the application is there.
void useAndroid();
// "FFmpeg 8.1.2 (libavformat 62.12, libavcodec 62.28, libavfilter 11.14)"; empty without them.
QString version();

// What MediaProbe reads with ffprobe, read in-process: as fast as opening the file, and
// no ffprobe needed. False when the file cannot be read, with why in `error`, or when the
// program was built without the libraries.
bool probe(const QString &videoPath, MediaInfo *info, QString *error = nullptr);
// The length of a sound or video file in seconds, as ffprobe's format=duration; 0 when it
// cannot be read or without the libraries.
double duration(const QString &path);

// Whether the video's pictures change their H.264 or HEVC settings inside the stream, away from
// those in the file's header: recordings of two cameras or two modes joined into one file without
// encoding again. A player that has shown such a part keeps its settings, and the parts before it,
// which never restate theirs, come out broken after a seek back. Some places along the file are
// looked at, not all of it; false without the libraries.
bool changesSettings(const QString &path);

// What VideoJoin::arguments has ffmpeg do, done in-process: the files `paths` copied one
// after another into `output`, the first picture, all sound and, with `cameraData`, the
// camera's data. Unlike ffmpeg's concat, files whose clocks differ (Matroska and MP4) are
// copied right. Blocking; `progress` is told how far the output has got (seconds) now and
// then, and stops the join by answering false. A failed or stopped join leaves an unfinished
// file, with why in `error` (empty when stopped).
bool copyJoin(const QStringList &paths, const QString &output, bool cameraData,
              const std::function<bool(double seconds)> &progress, QString *error);

// The encoder is in the libraries.
bool hasEncoder(const QString &id);
// A tiny test encode with it succeeds, with `options` (ffmpeg's flags, "-c:v" left out) and
// on the VA-API device `vaapiDevice` when not empty: what tells whether a graphics card's
// encoder really works here. Blocking, a fraction of a second; call from a worker thread.
bool encoderWorks(const QString &id, const QStringList &options, const QString &vaapiDevice = QString());
}
