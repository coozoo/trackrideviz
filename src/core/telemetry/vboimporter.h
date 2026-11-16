#pragma once

#include "telemetrysession.h"

class QIODevice;

// Racelogic VBO, the text files of VBOX data loggers and lap timers.
//
// Sections in square brackets; [column names] says what each column of [data] holds.
// Positions are in minutes, longitude counting positive to the west; speed in km/h; times
// UTC, the date from the "File created on" line. Speed, heading, height, vertical speed and
// the G-forces go to the channels of their kind; any other column is kept under its own name
// (an engine's rpm, a throttle) with the unit [channel units] gives it.
class VboImporter
{
public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(QIODevice *device, QString *error = nullptr);
};
