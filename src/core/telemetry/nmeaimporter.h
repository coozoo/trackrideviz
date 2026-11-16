#pragma once

#include "telemetrysession.h"

class QIODevice;

// NMEA 0183 logs: the sentences GPS receivers send, one a line ($GPRMC, $GNGGA...), as
// lap timers, dash cameras and data loggers write them down.
//
// RMC gives the position, the speed over ground, the course and the date; GGA the height
// above the sea. Sentences of one moment are taken together; a line whose checksum does not
// match is left out, as are moments without a fix. Times are UTC; a log going on past
// midnight goes on into the next day.
class NmeaImporter
{
public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(QIODevice *device, QString *error = nullptr);
    // Whether the start of a file reads as NMEA: for .txt and .log, which other loggers write too.
    static bool looksLikeNmea(const QString &path);
};
