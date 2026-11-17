#pragma once

#include "../sync/syncmap.h"
#include "exportsettings.h"
#include "mediainfo.h"
#include "videoencoders.h"

#include <QCoreApplication>
#include <QStringList>

// Several video files made one, one after another. Files of the same kind (camera chapters,
// recordings of one camera with one setting) have their streams copied as they are: fast and
// without loss. Others are encoded again.
class VideoJoin
{
    Q_DECLARE_TR_FUNCTIONS(VideoJoin)

public:
    struct Part
    {
        QString path;
        MediaInfo info;
    };

    // In recording order: by name, except that camera chapters of one
    // recording (GX010123, GX020123...) follow each other.
    static QStringList ordered(const QStringList &paths);

    // The camera chapters of the recording `path` belongs to, beside it on the disk, from
    // `path`'s chapter on, in order. `path` may be a chapter (GX020123.MP4) or a video joined
    // from them and named after the first ("GX010123_joined.MP4"). Empty for any other file.
    static QStringList chaptersFrom(const QString &path);

    // Why the parts cannot be joined by copying (they can by encodeArguments); empty when they can.
    static QString whyNot(const QVector<Part> &parts);

    // The break before each part after the first, from when each was
    // recorded (the date and time in the file name, else the file's own
    // creation time). Parts that follow straight on (chapters) get none.
    static QVector<TimeShift> shifts(const QVector<Part> &parts);

    // An ffmpeg concat list naming the parts.
    static QByteArray listFile(const QStringList &paths);
    // Whether the camera's data track goes into the join: every part has one, and `output` is a
    // container that holds it (.mp4, .mov).
    static bool keepsCameraData(const QVector<Part> &parts, const QString &output);
    // ffmpeg arguments that join what `listFile` names into `output`.
    static QStringList arguments(const QString &listFile, const QString &output, bool cameraData = false);

    // Joining videos of different kinds: each encoded again at the first one's size (the rest
    // scaled into it, with black bars where the shape differs) and frame rate, sound made
    // 48 kHz stereo AAC (silence for a part without any). With `encoder` and the rate of
    // `settings`. ffmpeg writes its progress to its standard output (see progressSeconds).
    // The camera's data track cannot come along.
    static QStringList encodeArguments(const QVector<Part> &parts, const QString &output, const VideoEncoder &encoder,
                                       const ExportSettings &settings);
    // How far ffmpeg's -progress output says it got, in seconds of the output; -1 when the
    // text holds no position.
    static double progressSeconds(const QByteArray &progressOutput);
};
