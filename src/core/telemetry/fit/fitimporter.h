#pragma once

#include "../telemetrysession.h"

#include <QCoreApplication>

// Builds a TelemetrySession from a FIT activity file.
//
// - `record` messages become channels. Known fields get canonical ids and SI
//   units (see channelids.h); developer fields become "dev.<name>" channels;
//   native fields this importer has no name for are kept as "fit.record.<n>"
//   with their raw value, so nothing a device recorded is silently lost.
// - `lap` messages become laps, timer stop/start `event`s become pauses,
//   `activity.local_timestamp` gives the UTC offset.
// - Time 0 is the first record. Records are not assumed to be 1 Hz.
class FitImporter
{
    Q_DECLARE_TR_FUNCTIONS(FitImporter)

public:
    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
    static TelemetrySessionPtr read(const QByteArray &data, QString *error = nullptr);
};
