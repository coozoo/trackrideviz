#pragma once

#include "../telemetry/telemetrysession.h"

#include <QCoreApplication>
#include <QDateTime>

// Finds where a video sits on the telemetry timeline.
//
// The result is one number, the sync offset:
//     telemetry time = video time + offset
// i.e. the telemetry time (seconds from session start) of the first video frame.
class VideoSync
{
    Q_DECLARE_TR_FUNCTIONS(VideoSync)

public:
    struct Result
    {
        bool ok{false};
        double offsetSeconds{0.0};
        // User-facing: what the offset is based on, or why there is none.
        QString explanation;
    };

    // Recording start as the camera wrote it into the file name:
    // "20250715_074932A.mp4", "VID_20250715_074932.mp4", "2025-07-15 07-49-32.mkv".
    // That is the camera's wall clock with no time zone; it is returned as if
    // it were UTC. Invalid when the name holds no date and time.
    static QDateTime wallClockFromFileName(const QString &fileName);

    // Clock match. The data's own UTC offset turns the wall clock into UTC;
    // fallbackUtcOffsetSeconds (normally this computer's time zone) is used
    // when the data does not state one. If that leaves the video outside the
    // recording, the nearest shift by whole hours (failing that, half and
    // quarter hours) that makes them overlap is taken instead: cameras set to
    // another time zone and exports that label local time as UTC are both
    // common. Such a guess can be an hour off, so the explanation says so.
    // videoDuration may be 0 when not known yet.
    static Result match(const TelemetrySession &session, const QDateTime &videoWallClock,
                        double videoDuration, int fallbackUtcOffsetSeconds);
};
