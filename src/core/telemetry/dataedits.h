#pragma once

#include "telemetrysession.h"

#include <QVector>

// A correction of the recorded data: the samples of one channel between two
// times (telemetry time) are left out, and the channel is read across the
// gap as if they were never there - a GPS jump, a sensor dropout.
struct DataEdit
{
    QString channel;
    double start{0.0};
    double end{0.0};

    bool operator==(const DataEdit &other) const
    {
        return channel == other.channel && start == other.start && end == other.end;
    }
};

namespace DataEdits
{
// `recorded` with the edits applied; `recorded` itself when there are none.
// Latitude and longitude are one position: an edit of either takes out both.
TelemetrySessionPtr apply(const TelemetrySessionPtr &recorded, const QVector<DataEdit> &edits);

// `edits` without what overlaps start..end of `channel`.
QVector<DataEdit> restored(const QVector<DataEdit> &edits, const QString &channel, double start, double end);
}
