#pragma once

#include "telemetrysession.h"

class QIODevice;

// GPX 1.0/1.1 track reader.
//
// All <trk>/<trkseg> are read as one continuous recording. Points without a
// <time> are skipped: without time there is nothing to sync to video.
// Extensions are matched by element name regardless of namespace, which covers
// Garmin TrackPointExtension v1/v2 (hr, cad, atemp, speed, course), Garmin
// PowerExtension (PowerInWatts) and the bare <power> many apps write.
// Speed and distance are derived from positions when the file has none.
class GpxImporter
{
public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(QIODevice *device, QString *error = nullptr);
};
