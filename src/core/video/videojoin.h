#pragma once

#include "../sync/syncmap.h"
#include "mediainfo.h"

#include <QCoreApplication>
#include <QStringList>

// Several video files made one, one after another, the streams copied as
// they are: fast and without loss, but only for files of the same kind
// (camera chapters, recordings of one camera with one setting).
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

    // Why the parts cannot be joined by copying; empty when they can.
    static QString whyNot(const QVector<Part> &parts);

    // The break before each part after the first, from when each was
    // recorded (the date and time in the file name, else the file's own
    // creation time). Parts that follow straight on (chapters) get none.
    static QVector<TimeShift> shifts(const QVector<Part> &parts);

    // An ffmpeg concat list naming the parts.
    static QByteArray listFile(const QStringList &paths);
    // ffmpeg arguments that join what `listFile` names into `output`.
    static QStringList arguments(const QString &listFile, const QString &output);
};
