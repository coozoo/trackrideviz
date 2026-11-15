#pragma once

#include "telemetrysession.h"

class QIODevice;

// TCX (Training Center XML) reader, the format of sports watches and bike
// computers beside FIT.
//
// Every <Trackpoint> with a <Time> of every activity, lap and track is read
// as one continuous recording: position, altitude, distance, heart rate,
// cadence, and from the extensions speed and power (<Speed>, <Watts>, matched
// by name whatever the namespace). Points without a position still count:
// an indoor ride has none. The file's laps are kept when it has more than
// one. What it lacks (speed, heading, grade) is worked out later as for any
// other source.
class TcxImporter
{
public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(QIODevice *device, QString *error = nullptr);
};
