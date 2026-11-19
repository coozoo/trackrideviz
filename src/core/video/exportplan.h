#pragma once

#include "cutlist.h"
#include "exportsettings.h"
#include "mediainfo.h"
#include "soundtrack.h"

#include <QRect>
#include <QStringList>

class Overlay;

// Everything worked out before an export starts: what goes in, the filter graph that
// puts it together, how it is encoded and stored, and what the application has to
// feed into it. The ffmpeg command line is made from these parts; an engine of the
// program's own (libav) runs the same parts in-process.
//
// ffmpeg reads and decodes the source video itself. The application sends
// only the gauges, through ffmpeg's standard input: for every video frame one
// raw picture (Qt's premultiplied ARGB32) holding each region with gauges,
// stacked one above the other. ffmpeg cuts the regions apart again, lays
// each where it belongs, encodes, and passes the sound through. Gauges in two
// corners so cost two small regions, not the whole frame.
struct ExportPlan
{
    // One input of the filter graph, numbered by its place: [0:v], [1:v], [2:a]...
    struct Input
    {
        QStringList options;    // ffmpeg's, as they stand before its -i: "-ss", "12.000"
        QString source;         // a file; Pipe for the gauges the application draws
    };
    static constexpr const char *Pipe = "pipe:0";

    // The parts. Options are kept as ffmpeg writes them ("-b:v", "20000k"); libav knows
    // them by the same names without the dash and the stream (b, crf, preset, movflags).
    QStringList globalOptions;  // before the inputs: the graphics card to encode on
    QVector<Input> inputs;
    QString filterGraph;        // -filter_complex: ends in [v], and [a] when the sound is made anew
    // The sound: "[a]" from the graph, "0:a?" the video's own as it is; empty: none.
    QString audioMap;
    QStringList audioOptions;   // "-c:a", "copy" / "aac", "-b:a", "192k"...
    QStringList videoOptions;   // "-c:v", "libx264", "-b:v", "20000k"...

    QStringList arguments;      // for ffmpeg: all of the above in its order
    QSize outputSize;
    // Parts of the output frame with gauges on them. None: nothing is drawn,
    // no pictures are expected and the video is simply re-encoded.
    QVector<QRect> regions;
    QVector<int> stackTops;     // where each region starts in the piped picture
    QSize stackSize{0, 0};      // the piped picture (QSize() would be -1 x -1)
    // Smallest rectangle holding all regions.
    QRect overlayRect;
    int fpsNum{0};              // rate of the piped pictures = rate of the video
    int fpsDen{1};
    // Pictures to send. The output ends with the last of them: closing the
    // pipe earlier is how an export is stopped with a playable file.
    qint64 frameCount{0};
    double startTime{0.0};      // video time of the first one
    // With cuts, the parts of the video that are exported, one after another
    // (video time); empty: all from startTime on.
    QVector<TimeRange> kept;
    QString outputPath;
    bool hasAudio{false};       // the output gets a sound track
    // The gauges alone over nothing: the pictures are sent with straight alpha.
    bool transparent{false};
    QStringList muxerArguments; // container options, needed again if the file is rewritten

    // Video time of picture `index`.
    double frameTime(qint64 index) const
    {
        // a video whose rate could not be read is never exported (MediaInfo::isValid); 0 all the same
        const double into = fpsNum > 0 ? double(index) * fpsDen / fpsNum : 0.0;
        return kept.isEmpty() ? startTime + into : CutList::sourceTime(kept, into);
    }
    // Length of `frames` pictures in seconds.
    double lengthOf(qint64 frames) const { return fpsNum > 0 ? double(frames) * fpsDen / fpsNum : 0.0; }

    // The ffmpeg command line made of the parts (what `arguments` holds).
    QStringList ffmpegArguments() const;

    // `cuts`: parts of the video left out (video time). `soundtrack`: music and volumes.
    static ExportPlan make(const QString &videoPath, const MediaInfo &info,
                           const ExportSettings &settings, const Overlay &overlay,
                           const QVector<TimeRange> &cuts = {}, const Soundtrack &soundtrack = {});
    // Output size for a wanted height (0 = keep): never upscaled, always even.
    static QSize outputSizeFor(const QSize &source, int wantedHeight);
};
