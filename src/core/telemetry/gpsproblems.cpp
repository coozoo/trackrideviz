#include "gpsproblems.h"

#include "channelids.h"
#include "geo.h"

#include <algorithm>
#include <cmath>

namespace
{
// a gap is this long at least, and this many times the usual time between positions
constexpr double kGapSeconds = 10.0;
constexpr double kGapIntervals = 4.0;
// and the rider went on across it at this speed at least: a device that records nothing while
// standing (some do, with no pause in the file) leaves gaps that are stops
constexpr double kGapMoving = 2.0;         // m/s
// a jump goes faster than this many times the speed most of the ride is below, and this at least
constexpr double kJumpTimes = 3.0;
constexpr double kJumpMinimum = 20.0;      // m/s
// what runs off the way for longer is the way, and the position it was taken from the error
constexpr double kJumpLongest = 60.0;      // s
// and it goes this far off the line from where it left the way to where it came back at least:
// positions catching up on the line after a stale one are no jump
constexpr double kJumpOffLine = 30.0;      // m
// frozen: moving less than this from one position to the next...
constexpr double kFrozenMeters = 1.0;
// ... for this long at least, while the speed sensor says more than this
constexpr double kFrozenSeconds = 5.0;
constexpr double kFrozenSpeed = 3.0;       // m/s

struct Fix
{
    double t{0.0};
    double lat{0.0};
    double lon{0.0};
};

double meters(const Fix &a, const Fix &b)
{
    return Geo::distanceMeters(a.lat, a.lon, b.lat, b.lon);
}

// How far `p` is from the line a-b, meters; near enough flat for the few hundred meters asked about.
double offLine(const Fix &a, const Fix &b, const Fix &p)
{
    const double toMeters = 111195.0;
    const double cosLat = std::cos(a.lat * M_PI / 180.0);
    const double bx = (b.lon - a.lon) * cosLat * toMeters;
    const double by = (b.lat - a.lat) * toMeters;
    const double px = (p.lon - a.lon) * cosLat * toMeters;
    const double py = (p.lat - a.lat) * toMeters;
    const double length2 = bx * bx + by * by;
    const double along = length2 > 0.0 ? std::clamp((px * bx + py * by) / length2, 0.0, 1.0) : 0.0;
    return std::hypot(px - along * bx, py - along * by);
}

double quantile(QVector<double> values, double q)
{
    if (values.isEmpty())
        {
            return 0.0;
        }
    const int at = std::clamp(int(q * (values.size() - 1)), 0, int(values.size()) - 1);
    std::nth_element(values.begin(), values.begin() + at, values.end());
    return values.at(at);
}

bool inPause(const TelemetrySession &session, double from, double to)
{
    return std::any_of(session.pauses().cbegin(), session.pauses().cend(), [from, to](const TelemetrySession::Pause &pause)
    {
        return pause.startTime < to && pause.endTime > from;
    });
}
}

DataEdit GpsProblems::Problem::fix() const
{
    return {ChannelId::PositionLat, start, end};
}

QVector<GpsProblems::Problem> GpsProblems::find(const TelemetrySession &session, const QVector<DataEdit> &edits)
{
    QVector<Problem> found;
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    if (!lat || !lon || lat->count() < 3)
        {
            return found;
        }
    QVector<Fix> fixes;
    fixes.reserve(lat->count());
    for (int i = 0; i < lat->count(); ++i)
        {
            bool ok = false;
            const double longitude = lon->valueAt(lat->timeAt(i), &ok);
            if (ok)
                {
                    fixes.append({lat->timeAt(i), lat->valueAtIndex(i), longitude});
                }
        }
    const int n = int(fixes.size());
    if (n < 3)
        {
            return found;
        }
    QVector<double> intervals;
    QVector<double> speeds;
    for (int i = 1; i < n; ++i)
        {
            const double dt = fixes[i].t - fixes[i - 1].t;
            if (dt > 0.0)
                {
                    intervals.append(dt);
                    speeds.append(meters(fixes[i - 1], fixes[i]) / dt);
                }
        }

    // gaps
    const double gapLongest = std::max(kGapSeconds, kGapIntervals * quantile(intervals, 0.5));
    for (int i = 1; i < n; ++i)
        {
            const Fix &a = fixes[i - 1];
            const Fix &b = fixes[i];
            const bool corrected = std::any_of(edits.cbegin(), edits.cend(), [&a, &b](const DataEdit &edit)
            {
                return (edit.channel == ChannelId::PositionLat || edit.channel == ChannelId::PositionLon)
                       && edit.start > a.t && edit.end < b.t;
            });
            const bool moving = meters(a, b) / (b.t - a.t) >= kGapMoving;
            if (b.t - a.t > gapLongest && moving && !inPause(session, a.t, b.t) && !corrected)
                {
                    found.append({Problem::Gap, a.t, b.t, meters(a, b)});
                }
        }

    // frozen: standing still while the wheel turns. First: where it starts again it catches up,
    // which is no jump
    QVector<bool> stuck(n, false);
    const Channel *speed = session.channel(ChannelId::Speed);
    if (speed && !session.derivedChannels().contains(ChannelId::Speed))
        {
            int still = -1;   // the last position before it froze
            auto close = [&](int last)
            {
                if (still >= 0 && last > still && fixes[last].t - fixes[still].t >= kFrozenSeconds)
                    {
                        found.append({Problem::Frozen, fixes[still + 1].t, fixes[last].t, 0.0});
                        std::fill(stuck.begin() + still + 1, stuck.begin() + last + 1, true);
                    }
                still = -1;
            };
            for (int i = 1; i < n; ++i)
                {
                    bool ok = false;
                    const double sensed = speed->valueAt(fixes[i].t, &ok);
                    const bool frozen = ok && sensed > kFrozenSpeed && meters(fixes[i - 1], fixes[i]) < kFrozenMeters;
                    if (frozen && still < 0)
                        {
                            still = i - 1;
                        }
                    else if (!frozen)
                        {
                            close(i - 1);
                        }
                }
            close(n - 1);
        }

    // jumps: positions the last good one could not have reached
    const double limit = std::max(kJumpMinimum, kJumpTimes * quantile(speeds, 0.9));
    int good = 0;
    int runStart = -1;
    for (int i = 1; i < n; ++i)
        {
            if (stuck[i])
                {
                    continue;
                }
            const double dt = fixes[i].t - fixes[good].t;
            const bool reachable = dt > 0.0 && meters(fixes[good], fixes[i]) / dt <= limit;
            if (!reachable && (runStart < 0 || fixes[i].t - fixes[runStart].t <= kJumpLongest))
                {
                    if (runStart < 0)
                        {
                            runStart = i;
                        }
                    continue;
                }
            if (runStart >= 0 && reachable)
                {
                    double farthest = 0.0;
                    for (int j = runStart; j < i; ++j)
                        {
                            farthest = std::max(farthest, offLine(fixes[good], fixes[i], fixes[j]));
                        }
                    if (farthest > kJumpOffLine)
                        {
                            found.append({Problem::Jump, fixes[runStart].t, fixes[i - 1].t, farthest});
                        }
                }
            // back on the way, or so long off it that the way was where it went
            runStart = -1;
            good = i;
        }

    std::sort(found.begin(), found.end(), [](const Problem &a, const Problem &b)
    {
        return a.start < b.start;
    });
    return found;
}
