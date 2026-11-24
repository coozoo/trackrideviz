#pragma once

#include "../telemetry/telemetrysession.h"

#include <QPointF>

// A line across the track: a lap begins wherever the route crosses it.
struct StartFinishLine
{
    double lat1{0.0};
    double lon1{0.0};
    double lat2{0.0};
    double lon2{0.0};

    bool isValid() const { return lat1 != lat2 || lon1 != lon2; }
    bool operator==(const StartFinishLine &other) const
    {
        return lat1 == other.lat1 && lon1 == other.lon1 && lat2 == other.lat2 && lon2 == other.lon2;
    }
    bool operator!=(const StartFinishLine &other) const { return !(*this == other); }
};

// Laps and what gauges show about them.
//
// With a start/finish line, laps are the stretches between two crossings of
// it in the same direction as the first one; a crossing sooner than
// `minimumLap` after the last is GPS jitter at the line, not a lap. Without
// one, the laps the device recorded are used. A ride from one place to
// another crosses its line only once: the last sector line it comes to is
// then the finish, and the stretch from the line to it is the one lap.
//
// From the laps come the lap channels (channelids.h): the lap under way, its
// running time, the previous and the best lap so far, the number of laps,
// and the delta to the best lap so far at the same point of the lap (by
// distance). The stretch before the first crossing is no lap; the one after
// the last is, unfinished.
namespace LapEngine
{
const double DefaultMinimumLap = 20.0;   // seconds
const double DefaultLineWidth = 40.0;    // meters: a track and GPS error on both sides

// The finished laps between crossings of `line`.
QVector<TelemetrySession::Lap> detect(const TelemetrySession &session, const StartFinishLine &line,
                                      double minimumLap = DefaultMinimumLap);

// A line `width` wide across the direction of travel where the route is at `time`.
// Invalid without positions there.
StartFinishLine lineAt(const TelemetrySession &session, double time, double width = DefaultLineWidth);

// Where the route crosses each of `sectors` between `start` and `end` (the first
// time, either way), in order of time. Lines not crossed there are missing.
QVector<double> splits(const TelemetrySession &session, const QVector<StartFinishLine> &sectors, double start,
                       double end);

// How wide a line at the route's place at `time` has to be to be crossed on every pass by it:
// the default, or wider when other passes went by farther to the side (a lap ridden a little
// apart, GPS drift), up to MaximumLineWidth.
double lineWidthAt(const TelemetrySession &session, double time);
const double MaximumLineWidth = 120.0;   // meters: wider would catch a road beside it

// Where a start/finish line gives the most finished laps (two at least), each line tried across
// the route every few seconds; of those as good, the one whose laps cover the most of the ride.
// Invalid when no place gives two.
StartFinishLine findLine(const TelemetrySession &session, double minimumLap = DefaultMinimumLap);

// Every time the route passed a place: the moment of each pass that came within `within`
// meters of it, in order of time. A route ridden out and back, or lap after lap, has several.
QVector<double> passes(const TelemetrySession &session, double lat, double lon, double within);

// When the route came nearest to a place; -1 without positions. *distance: how near, in meters.
double nearestTime(const TelemetrySession &session, double lat, double lon, double *distance = nullptr);

// One lap against distance into it: (meters, channel value as stored) every
// `step` meters. Empty without distance or the channel.
QVector<QPointF> byDistance(const TelemetrySession &session, const TelemetrySession::Lap &lap, const QString &channelId,
                            double step = 5.0);

// How far behind `reference` (+, seconds) `lap` is at the same point of the
// lap, against distance into `reference` in meters. Points are matched by
// how much of the lap is done, so laps of a slightly different length line up.
QVector<QPointF> deltaByDistance(const TelemetrySession &session, const TelemetrySession::Lap &reference,
                                 const TelemetrySession::Lap &lap, double step = 5.0);

// `base` with the laps from `line` (when valid) and the lap channels. Lap
// channels already in `base` are kept unless a line is given. With `sectors`
// (lines across the track inside the lap), each lap that crosses all of them
// gets its splits, and the sector channels are added: sector, its running
// time, the previous sector's time and that against the best of it so far.
// `reference`: the lap every lap is measured against (ReferenceDelta), by its number from 1;
// 0, or one that is not there: the fastest finished lap.
TelemetrySessionPtr apply(const TelemetrySessionPtr &base, const StartFinishLine &line,
                          const QVector<StartFinishLine> &sectors = {}, int reference = 0);
}
