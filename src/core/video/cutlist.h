#pragma once

#include "../telemetry/telemetrysession.h"

#include <QVector>

// A stretch of video time, start < end, in seconds.
struct TimeRange
{
    double start{0.0};
    double end{0.0};

    double length() const { return end - start; }
    bool contains(double t) const { return t >= start && t < end; }
    bool operator==(const TimeRange &other) const { return start == other.start && end == other.end; }
    bool operator!=(const TimeRange &other) const { return !(*this == other); }
};

// Parts of the video left out of the export (standing at lights, a stop).
// The video and its data are not changed: the export goes from the end of
// what is kept straight to the next kept part, and gauges keep showing the
// real values of the moments that are kept.
namespace CutList
{
// Sorted, overlapping and touching ones joined, inside 0..duration (duration <= 0: no upper bound).
QVector<TimeRange> normalized(const QVector<TimeRange> &cuts, double duration = 0.0);

// `cuts` with `range` added, or removed again where it overlaps them.
QVector<TimeRange> added(const QVector<TimeRange> &cuts, const TimeRange &range);
QVector<TimeRange> removed(const QVector<TimeRange> &cuts, const TimeRange &range);

// What of from..to is kept.
QVector<TimeRange> kept(const QVector<TimeRange> &cuts, double from, double to);
double keptLength(const QVector<TimeRange> &kept);

// Video time `outputTime` seconds into the kept parts, one after another.
double sourceTime(const QVector<TimeRange> &kept, double outputTime);
// The cut holding `t`, or nullptr.
const TimeRange *cutAt(const QVector<TimeRange> &cuts, double t);

// Where the data stood still (speed below `maxSpeed` m/s for at least
// `minimum` s), in video time (telemetry time = video time + offset), within
// 0..videoDuration. `margin` s at each end stay, so the stop and the start are seen.
QVector<TimeRange> stops(const TelemetrySession &session, double offset, double videoDuration,
                         double minimum = 10.0, double maxSpeed = 0.5, double margin = 2.0);
}
