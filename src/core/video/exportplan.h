#pragma once

#include "exportsettings.h"
#include "mediainfo.h"

#include <QRect>
#include <QStringList>

class Overlay;

// Everything worked out before an export starts: the ffmpeg command line and
// what the application has to feed into it.
//
// ffmpeg reads and decodes the source video itself. The application sends
// only the gauges: raw RGBA pictures of `overlayRect`, one per video frame,
// through ffmpeg's standard input. ffmpeg lays them over the video, encodes,
// and passes the sound through.
struct ExportPlan
{
    QStringList arguments;      // for ffmpeg
    QSize outputSize;
    // Part of the output frame the piped pictures cover. Empty: nothing is
    // drawn, no pictures are expected and the video is simply re-encoded.
    QRect overlayRect;
    int fpsNum{0};              // rate of the piped pictures = rate of the video
    int fpsDen{1};
    // Pictures to send. The output ends with the last of them: closing the
    // pipe earlier is how an export is stopped with a playable file.
    qint64 frameCount{0};
    double startTime{0.0};      // video time of the first one
    QString outputPath;
    bool hasAudio{false};       // the output gets a sound track
    QStringList muxerArguments; // container options, needed again if the file is rewritten

    // Video time of picture `index`.
    double frameTime(qint64 index) const { return startTime + double(index) * fpsDen / fpsNum; }
    // Length of `frames` pictures in seconds.
    double lengthOf(qint64 frames) const { return double(frames) * fpsDen / fpsNum; }

    static ExportPlan make(const QString &videoPath, const MediaInfo &info,
                           const ExportSettings &settings, const Overlay &overlay);
    // Output size for a wanted height (0 = keep): never upscaled, always even.
    static QSize outputSizeFor(const QSize &source, int wantedHeight);
};
