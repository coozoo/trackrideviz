#pragma once

#include "telemetrysession.h"

#include <QCoreApplication>
#include <QString>

// Saves a recording, corrections and all, in a form other programs and this one read again.
//
// GPX: the track (positions with their height and time) and, on each point, the heart rate,
// cadence, power, temperature and speed in the extensions sport sites read.
// CSV: every channel, one column each named by its id, in the units channels store, a row for
// every moment any of them has a sample; the program's own data profile reads it back.
// Laps are in neither.
class TelemetryWriter
{
    Q_DECLARE_TR_FUNCTIONS(TelemetryWriter)

public:
    // The first line of the CSV files, by which the data profile knows them.
    static constexpr char CsvMark[] = "Race Track Visualizer data";

    static QByteArray gpx(const TelemetrySession &session);
    static QByteArray csv(const TelemetrySession &session);
    // By the suffix: .gpx, otherwise CSV.
    static bool save(const TelemetrySession &session, const QString &path, QString *error = nullptr);
};
