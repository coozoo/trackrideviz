#pragma once

#include "telemetrysession.h"

class QIODevice;

// Reads the positions a GoPro camera writes into its own video files.
//
// The MP4 has a track of "gpmd" samples beside picture and sound, about one
// a second, each a tree of key-length-value records (GPMF). Read from it:
// GPS5 (latitude, longitude, altitude, speed over ground, speed in space;
// cameras up to the HERO10) or GPS9 (the same with time, precision and fix in
// every sample; HERO11 on), scaled by the SCAL beside them, and the clock of
// GPSU. Samples taken without a fix are left out: the camera fills them
// with wherever it was last.
//
// Times are those of the video: the first frame is 0, so the data of a video
// needs no synchronizing with it. Chapters of one recording are separate
// files and are read one by one.
class GpmfImporter
{
public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(QIODevice *device, QString *error = nullptr);

    // One sample of the data track on the video's clock: for tests.
    struct Payload
    {
        double start{0.0};      // s
        double duration{0.0};   // s
        QByteArray data;
    };
    // The session those samples give; nullptr when none of them holds a position with a fix.
    static TelemetrySessionPtr fromPayloads(const QVector<Payload> &payloads);
};
