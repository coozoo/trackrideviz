#include "lapengine.h"

#include "../telemetry/channelids.h"
#include "../telemetry/geo.h"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double kPi = 3.14159265358979323846;
const double kEarthRadius = 6371008.8;
// a lap closed where it passes by the line: coming back at most this many degrees off the way it
// crossed, not along the way it came, ridden the other way
const double kMostTurnedBack = 120.0;
// ... unless it went round a loop: enclosing this much of its length squared at least (a circle 0.08)
const double kLeastEnclosed = 0.01;
const char *const kLapChannels[] =
{
    ChannelId::Lap, ChannelId::LapTime, ChannelId::PreviousLapTime, ChannelId::BestLapTime,
    ChannelId::LapDelta, ChannelId::LapCount, ChannelId::Sector, ChannelId::SectorTime, ChannelId::PreviousSectorTime,
    ChannelId::SectorDelta, ChannelId::BestLapNumber, ChannelId::SpeedLapMax, ChannelId::SpeedLapAvg, ChannelId::PowerLapAvg,
    ChannelId::ReferenceDelta, ChannelId::ReferenceLapTime,
};

// The top and the average speed and the average power of each stretch so far, starting
// again with every stretch: the laps, or the whole recording as one.
void addRunningFigures(TelemetrySession &session, const QVector<QPair<double, double>> &stretches)
{
    const Channel *speed = session.channel(ChannelId::Speed);
    const Channel *power = session.channel(ChannelId::Power);
    auto fresh = [&session](const char *id, const QString &unit, const Channel *source) -> Channel *
    {
        const Channel *present = session.channel(id);
        if (!source || source->isEmpty() || (present && !present->isEmpty()))
            {
                return nullptr;
            }
        session.addDerivedChannel(id);
        return &session.channelForWriting(id, unit);
    };
    Channel *top = fresh(ChannelId::SpeedLapMax, "m/s", speed);
    Channel *average = fresh(ChannelId::SpeedLapAvg, "m/s", speed);
    Channel *averagePower = fresh(ChannelId::PowerLapAvg, "W", power);
    for (const auto &stretch : stretches)
        {
            double highest = 0.0;
            double speedSum = 0.0;
            double powerSum = 0.0;
            int speedCount = 0;
            int powerCount = 0;
            // a second apart is as fine as these figures are read
            for (double t = stretch.first; t < stretch.second; t += 1.0)
                {
                    bool ok = false;
                    const double v = speed ? speed->valueAt(t, &ok) : 0.0;
                    if (ok)
                        {
                            highest = std::max(highest, v);
                            speedSum += v;
                            ++speedCount;
                            if (top)
                                {
                                    top->append(t, highest);
                                }
                            if (average)
                                {
                                    average->append(t, speedSum / speedCount);
                                }
                        }
                    const double w = power ? power->valueAt(t, &ok) : 0.0;
                    if (power && ok)
                        {
                            powerSum += w;
                            ++powerCount;
                            if (averagePower)
                                {
                                    averagePower->append(t, powerSum / powerCount);
                                }
                        }
                }
        }
    session.removeEmptyChannels();
}

// Meters east and north of a reference point; good for a few kilometers.
struct Plane
{
    double lat0{0.0};
    double lon0{0.0};
    double cosLat{1.0};

    Plane(double lat, double lon)
        : lat0(lat)
        , lon0(lon)
        , cosLat(std::cos(lat * kPi / 180.0))
    {
    }
    QPointF toPlane(double lat, double lon) const
    {
        return QPointF((lon - lon0) * kPi / 180.0 * kEarthRadius * cosLat, (lat - lat0) * kPi / 180.0 * kEarthRadius);
    }
    void toLatLon(const QPointF &p, double *lat, double *lon) const
    {
        *lat = lat0 + p.y() / kEarthRadius * 180.0 / kPi;
        *lon = lon0 + p.x() / (kEarthRadius * cosLat) * 180.0 / kPi;
    }
};

double cross(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

struct Crossing
{
    double time;
    int side;   // which way: +1 or -1
};

// Whether the route from..to goes round something: the area it encloses against its length squared,
// as a circle's 1/4pi is; a road ridden there and back encloses next to nothing.
bool enclosesALoop(const TelemetrySession &session, double from, double to)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    bool ok = false;
    const double lat0 = lat->valueAt(from, &ok);
    if (!ok)
        {
            return false;
        }
    const Plane plane(lat0, lon->valueAt(from));
    double area = 0.0;
    double length = 0.0;
    QPointF previous;
    bool first = true;
    for (int i = 0; i < lat->count(); ++i)
        {
            const double t = lat->timeAt(i);
            if (t < from || t > to)
                {
                    continue;
                }
            const QPointF at = plane.toPlane(lat->valueAtIndex(i), lon->valueAt(t));
            if (!first)
                {
                    area += previous.x() * at.y() - at.x() * previous.y();
                    length += std::hypot(at.x() - previous.x(), at.y() - previous.y());
                }
            previous = at;
            first = false;
        }
    // closing it back to where it began adds nothing: that is the plane's origin
    return length > 0.0 && std::abs(area) / 2.0 / (length * length) >= kLeastEnclosed;
}

// The last lap of a course left at a junction just before the line: after the last crossing the
// route goes round the course again, comes back by the line without crossing it, and goes
// elsewhere. That lap ends where it came nearest the line; true when one was ended so.
// With a finished lap it has to have gone about as far again; with none (a loop ridden once), it
// has to have gone well away first. Either way it comes back the course's way, not along the way it
// came reversed (a road ridden out and home); with `reversed`, also back the way it went in, when
// what it rode encloses a loop (the end of a road to a loop and back).
bool closeLeftLap(const TelemetrySession &session, const StartFinishLine &line, QVector<TelemetrySession::Lap> &laps,
                  double minimumLap, bool reversed = true)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    const Channel *distance = session.channel(ChannelId::Distance);
    const Channel *heading = session.channel(ChannelId::Heading);
    if (!lat || !lon || !distance || !heading || laps.isEmpty())
        {
            return false;
        }
    const TelemetrySession::Lap last = laps.last();
    // the last crossing: the end of the finished lap, or the only one, which began the lap under way
    const bool finished = last.endTime > last.startTime;
    const double start = finished ? last.endTime : last.startTime;
    const double lapLength = finished ? distance->valueAt(last.endTime) - distance->valueAt(last.startTime) : 0.0;
    const double from = distance->valueAt(start);
    const double crossedHeading = heading->valueAt(start);
    const Plane plane((line.lat1 + line.lat2) / 2.0, (line.lon1 + line.lon2) / 2.0);
    // gone away: a lap's distance nearly, or (with none known) farther than the widest line twice
    const double awayFirst = 2.0 * LapEngine::MaximumLineWidth;
    bool wentAway = false;
    double nearest = LapEngine::MaximumLineWidth;
    double nearestTime = -1.0;
    for (int i = 0; i < lat->count(); ++i)
        {
            const double t = lat->timeAt(i);
            if (t <= start)
                {
                    continue;
                }
            const double done = distance->valueAt(t) - from;
            const QPointF at = plane.toPlane(lat->valueAtIndex(i), lon->valueAt(t));
            const double away = std::hypot(at.x(), at.y());
            if (finished ? done < 0.9 * lapLength : !wentAway)
                {
                    wentAway = wentAway || away > awayFirst;
                    continue;
                }
            if (finished && done > 1.2 * lapLength)
                {
                    break;
                }
            if (away < nearest)
                {
                    nearest = away;
                    nearestTime = t;
                }
            else if (!finished && nearestTime >= 0.0 && away > LapEngine::MaximumLineWidth)
                {
                    // the first time back by the line, and gone again
                    break;
                }
        }
    if (nearestTime < 0.0 || nearestTime - start < minimumLap)
        {
            return false;
        }
    // a ride that only ends where it began is a round trip, not a lap: the route has to go on elsewhere
    bool goesOn = false;
    for (int i = 0; i < lat->count() && !goesOn; ++i)
        {
            const double t = lat->timeAt(i);
            if (t > nearestTime)
                {
                    const QPointF at = plane.toPlane(lat->valueAtIndex(i), lon->valueAt(t));
                    goesOn = std::hypot(at.x(), at.y()) > LapEngine::MaximumLineWidth;
                }
        }
    if (!goesOn)
        {
            return false;
        }
    double turned = std::fmod(std::abs(heading->valueAt(nearestTime) - crossedHeading), 360.0);
    turned = std::min(turned, 360.0 - turned);
    if (turned > kMostTurnedBack && (!reversed || !enclosesALoop(session, start, nearestTime)))
        {
            return false;
        }
    if (finished)
        {
            laps.append({start, nearestTime});
        }
    else
        {
            laps.last().endTime = nearestTime;
        }
    return true;
}

// Every time the route crosses `line`, timed between the samples either side.
QVector<Crossing> crossingsOf(const TelemetrySession &session, const StartFinishLine &line)
{
    QVector<Crossing> crossings;
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    if (!line.isValid() || !lat || !lon || lat->count() < 2)
        {
            return crossings;
        }
    const Plane plane((line.lat1 + line.lat2) / 2.0, (line.lon1 + line.lon2) / 2.0);
    const QPointF a = plane.toPlane(line.lat1, line.lon1);
    const QPointF ab = plane.toPlane(line.lat2, line.lon2) - a;
    QPointF previous = plane.toPlane(lat->valueAtIndex(0), lon->valueAt(lat->timeAt(0)));
    for (int i = 1; i < lat->count(); ++i)
        {
            const double t0 = lat->timeAt(i - 1);
            const double t1 = lat->timeAt(i);
            const QPointF p = plane.toPlane(lat->valueAtIndex(i), lon->valueAt(t1));
            const QPointF step = p - previous;
            const double denominator = cross(step, ab);
            if (denominator != 0.0)
                {
                    // where along the step (u) and along the line (v) the two meet
                    const QPointF ap = a - previous;
                    const double u = cross(ap, ab) / denominator;
                    const double v = cross(ap, step) / denominator;
                    if (u >= 0.0 && u < 1.0 && v >= 0.0 && v <= 1.0)
                        {
                            crossings.append({t0 + u * (t1 - t0), denominator > 0.0 ? 1 : -1});
                        }
                }
            previous = p;
        }
    return crossings;
}

bool has(const TelemetrySession &session, const char *id)
{
    const Channel *channel = session.channel(id);
    return channel && !channel->isEmpty();
}

// How far into the lap (by distance, 0..1) the route is at each moment of a lap, as (fraction, seconds in).
QVector<QPointF> profile(const Channel &distance, double start, double end, double length)
{
    QVector<QPointF> points;
    const double d0 = distance.valueAt(start);
    for (double t = start; t < end; t += 0.25)
        {
            points.append(QPointF((distance.valueAt(t) - d0) / length, t - start));
        }
    points.append(QPointF(1.0, end - start));
    return points;
}

// Seconds the profiled lap took to reach `fraction`.
double timeAt(const QVector<QPointF> &profile, double fraction)
{
    const auto next = std::lower_bound(profile.cbegin(), profile.cend(), fraction,
                                       [](const QPointF &p, double f) { return p.x() < f; });
    if (next == profile.cbegin())
        {
            return profile.first().y();
        }
    if (next == profile.cend())
        {
            return profile.last().y();
        }
    const QPointF a = *(next - 1);
    const QPointF b = *next;
    return b.x() > a.x() ? a.y() + (b.y() - a.y()) * (fraction - a.x()) / (b.x() - a.x()) : b.y();
}
// Sector channels from the laps' splits and the lap under way (`openEnd` < 0: none).
void addSectorChannels(TelemetrySession &session, const QVector<TelemetrySession::Lap> &laps,
                       const QVector<double> &openSplits, double openStart, double openEnd, int count)
{
    for (const char *id : {ChannelId::Sector, ChannelId::SectorTime, ChannelId::PreviousSectorTime,
                           ChannelId::SectorDelta})
        {
            session.removeChannel(id);
            session.addDerivedChannel(id);
        }
    Channel &number = session.channelForWriting(ChannelId::Sector, QString(), Channel::Step);
    Channel &running = session.channelForWriting(ChannelId::SectorTime, "s", Channel::Linear);
    Channel &previous = session.channelForWriting(ChannelId::PreviousSectorTime, "s", Channel::Step);
    Channel &delta = session.channelForWriting(ChannelId::SectorDelta, "s", Channel::Step);
    QVector<double> best(count, std::numeric_limits<double>::max());
    auto lap = [&](const QVector<double> &bounds, bool finished)
    {
        for (int k = 0; k + 1 < bounds.size(); ++k)
            {
                const double from = bounds.at(k);
                const double to = bounds.at(k + 1);
                number.append(from, k + 1);
                running.append(from, 0.0);
                const bool done = finished || k + 2 < bounds.size();
                running.append(done ? to - 0.001 : to, (done ? to - 0.001 : to) - from);
                if (!done)
                    {
                        break;
                    }
                const double duration = to - from;
                previous.append(to, duration);
                // the first time round there is nothing to compare with
                delta.append(to, best.at(k) < std::numeric_limits<double>::max() ? duration - best.at(k) : 0.0);
                best[k] = std::min(best.at(k), duration);
            }
    };
    bool complete = false;
    for (const TelemetrySession::Lap &l : laps)
        {
            if (l.splits.size() + 1 == count)
                {
                    lap(QVector<double>{l.startTime} + l.splits + QVector<double>{l.endTime}, true);
                    complete = true;
                }
        }
    // only when the lines are known to work: a line the route misses would shift the numbers
    if (complete && openEnd > openStart && openSplits.size() < count)
        {
            // the lap under way: sectors finished so far, then the one being driven
            lap(QVector<double>{openStart} + openSplits + QVector<double>{openEnd}, false);
        }
    for (const char *id : {ChannelId::Sector, ChannelId::SectorTime, ChannelId::PreviousSectorTime,
                           ChannelId::SectorDelta})
        {
            const Channel *channel = session.channel(id);
            if (!channel || channel->isEmpty())
                {
                    session.removeChannel(id);
                }
        }
}
}

QVector<TelemetrySession::Lap> LapEngine::detect(const TelemetrySession &session, const StartFinishLine &line,
        double minimumLap)
{
    QVector<TelemetrySession::Lap> laps;
    QVector<double> crossings;
    int direction = 0;
    for (const Crossing &crossing : crossingsOf(session, line))
        {
            if ((direction == 0 || crossing.side == direction)
                    && (crossings.isEmpty() || crossing.time - crossings.last() >= minimumLap))
                {
                    direction = crossing.side;
                    crossings.append(crossing.time);
                }
        }
    for (int i = 1; i < crossings.size(); ++i)
        {
            laps.append({crossings.at(i - 1), crossings.at(i)});
        }
    // a single crossing still starts the first lap: it is under way to the end
    if (crossings.size() == 1)
        {
            laps.append({crossings.first(), crossings.first()});
        }
    return laps;
}

QVector<double> LapEngine::splits(const TelemetrySession &session, const QVector<StartFinishLine> &sectors,
                                  double start, double end)
{
    QVector<double> times;
    for (const StartFinishLine &line : sectors)
        {
            // the first time inside the lap, either way: a sector line is crossed once a lap
            for (const Crossing &crossing : crossingsOf(session, line))
                {
                    if (crossing.time > start && crossing.time < end)
                        {
                            times.append(crossing.time);
                            break;
                        }
                }
        }
    std::sort(times.begin(), times.end());
    return times;
}

StartFinishLine LapEngine::lineAt(const TelemetrySession &session, double time, double width)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    const Channel *heading = session.channel(ChannelId::Heading);
    if (!lat || !lon || !heading)
        {
            return StartFinishLine();
        }
    bool ok = false;
    const double la = lat->valueAt(time, &ok);
    const double lo = lon->valueAt(time);
    const double h = heading->valueAt(time) * kPi / 180.0;
    if (!ok)
        {
            return StartFinishLine();
        }
    // across the way: the heading turned a quarter
    const Plane plane(la, lo);
    const QPointF across(std::cos(h) * width / 2.0, -std::sin(h) * width / 2.0);
    StartFinishLine line;
    plane.toLatLon(-across, &line.lat1, &line.lon1);
    plane.toLatLon(across, &line.lat2, &line.lon2);
    return line;
}

StartFinishLine LapEngine::findLine(const TelemetrySession &session, double minimumLap)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    if (!lat || lat->count() < 2)
        {
            return StartFinishLine();
        }
    const Channel *distance = session.channel(ChannelId::Distance);
    // every few seconds: fine enough for a lap of a minute, quick enough for a ride of hours
    const double step = 5.0;
    QVector<StartFinishLine> lines;
    for (double t = lat->startTime(); t <= lat->endTime(); t += step)
        {
            const StartFinishLine line = lineAt(session, t, lineWidthAt(session, t));
            if (line.isValid())
                {
                    lines.append(line);
                }
        }
    // first laps that come round the course's way, the most of them covering the most; failing
    // those, a loop come back from the way it was entered (a road to a loop and back), the
    // shortest: the loop itself, not the road with it
    for (const bool reversed : {false, true})
        {
            StartFinishLine best;
            int bestLaps = 0;
            double bestCovered = 0.0;
            for (const StartFinishLine &line : std::as_const(lines))
                {
                    QVector<TelemetrySession::Lap> laps = detect(session, line, minimumLap);
                    closeLeftLap(session, line, laps, minimumLap, reversed);
                    int finished = 0;
                    double covered = 0.0;
                    for (const TelemetrySession::Lap &lap : std::as_const(laps))
                        {
                            // a "lap" hardly going anywhere is a stop the GPS wandered across the line in
                            const bool rode = distance && distance->valueAt(lap.endTime) - distance->valueAt(lap.startTime)
                                              >= 2.0 * MaximumLineWidth;
                            if (lap.endTime > lap.startTime && rode)
                                {
                                    ++finished;
                                    covered += lap.endTime - lap.startTime;
                                }
                        }
                    const bool better = reversed ? covered < bestCovered : covered > bestCovered;
                    if (finished > bestLaps || (finished == bestLaps && finished >= 1 && better))
                        {
                            best = line;
                            bestLaps = finished;
                            bestCovered = covered;
                        }
                }
            if (best.isValid())
                {
                    return best;
                }
        }
    return StartFinishLine();
}

double LapEngine::lineWidthAt(const TelemetrySession &session, double time)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    bool ok = false;
    const double la = lat ? lat->valueAt(time, &ok) : 0.0;
    if (!ok || !lon)
        {
            return DefaultLineWidth;
        }
    const double lo = lon->valueAt(time);
    // the farthest the other passes went by, with room for the GPS on each side
    const double margin = 10.0;
    double farthest = 0.0;
    for (double pass : passes(session, la, lo, MaximumLineWidth / 2.0 - margin))
        {
            if (std::abs(pass - time) > DefaultMinimumLap)
                {
                    farthest = std::max(farthest, Geo::distanceMeters(la, lo, lat->valueAt(pass), lon->valueAt(pass)));
                }
        }
    return std::clamp(2.0 * (farthest + margin), DefaultLineWidth, MaximumLineWidth);
}

QVector<double> LapEngine::passes(const TelemetrySession &session, double la, double lo, double within)
{
    QVector<double> times;
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    if (!lat || !lon)
        {
            return times;
        }
    // while the route stays near, the nearest moment; one pass ends when it has gone away again
    bool near = false;
    double best = 0.0;
    double bestTime = 0.0;
    for (int i = 0; i < lat->count(); ++i)
        {
            const double t = lat->timeAt(i);
            const double d = Geo::distanceMeters(la, lo, lat->valueAtIndex(i), lon->valueAt(t));
            if (d <= within)
                {
                    if (!near || d < best)
                        {
                            best = d;
                            bestTime = t;
                        }
                    near = true;
                }
            else if (near && d > within * 2.0)
                {
                    times.append(bestTime);
                    near = false;
                }
        }
    if (near)
        {
            times.append(bestTime);
        }
    return times;
}

double LapEngine::nearestTime(const TelemetrySession &session, double la, double lo, double *distance)
{
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    double best = std::numeric_limits<double>::max();
    double time = -1.0;
    if (lat && lon)
        {
            for (int i = 0; i < lat->count(); ++i)
                {
                    const double t = lat->timeAt(i);
                    const double d = Geo::distanceMeters(la, lo, lat->valueAtIndex(i), lon->valueAt(t));
                    if (d < best)
                        {
                            best = d;
                            time = t;
                        }
                }
        }
    if (distance)
        {
            *distance = time >= 0.0 ? best : -1.0;
        }
    return time;
}

QVector<QPointF> LapEngine::byDistance(const TelemetrySession &session, const TelemetrySession::Lap &lap,
                                       const QString &channelId, double step)
{
    QVector<QPointF> points;
    const Channel *distance = session.channel(ChannelId::Distance);
    const Channel *channel = session.channel(channelId);
    if (!distance || distance->isEmpty() || !channel || channel->isEmpty() || lap.endTime <= lap.startTime)
        {
            return points;
        }
    // through the lap in small steps of time, a point each time `step` more meters are done
    const double d0 = distance->valueAt(lap.startTime);
    double next = 0.0;
    for (double t = lap.startTime; t <= lap.endTime; t += 0.1)
        {
            const double into = distance->valueAt(t) - d0;
            if (into >= next)
                {
                    bool ok = false;
                    const double value = channel->valueAt(t, &ok);
                    if (ok)
                        {
                            points.append(QPointF(into, value));
                        }
                    next = (std::floor(into / step) + 1.0) * step;
                }
        }
    return points;
}

QVector<QPointF> LapEngine::deltaByDistance(const TelemetrySession &session, const TelemetrySession::Lap &reference,
        const TelemetrySession::Lap &lap, double step)
{
    QVector<QPointF> points;
    const Channel *distance = session.channel(ChannelId::Distance);
    if (!distance || distance->isEmpty())
        {
            return points;
        }
    const double referenceLength = distance->valueAt(reference.endTime) - distance->valueAt(reference.startTime);
    const double length = distance->valueAt(lap.endTime) - distance->valueAt(lap.startTime);
    if (referenceLength <= 0.0 || length <= 0.0)
        {
            return points;
        }
    const QVector<QPointF> referenceProfile = profile(*distance, reference.startTime, reference.endTime, referenceLength);
    const QVector<QPointF> lapProfile = profile(*distance, lap.startTime, lap.endTime, length);
    for (double into = 0.0; into <= referenceLength; into += step)
        {
            const double fraction = into / referenceLength;
            points.append(QPointF(into, timeAt(lapProfile, fraction) - timeAt(referenceProfile, fraction)));
        }
    return points;
}

TelemetrySessionPtr LapEngine::apply(const TelemetrySessionPtr &base, const StartFinishLine &line,
                                     const QVector<StartFinishLine> &givenSectors, int reference)
{
    QVector<StartFinishLine> sectors = givenSectors;
    bool openCourse = false;
    if (!base)
        {
            return base;
        }
    auto session = std::make_shared<TelemetrySession>(*base);
    // chosen again below, from the laps as they are now
    session->setReferenceLap({});
    double end = session->duration();
    QVector<TelemetrySession::Lap> laps = session->laps();
    if (line.isValid())
        {
            laps = detect(*session, line);
            // the course left just before the line: the last lap ends where it came nearest, no lap after it
            openCourse = closeLeftLap(*session, line, laps, DefaultMinimumLap);
            // A ride from one place to another crosses its start line once. The last of the
            // other lines it comes to is then the finish, and the stretch between is the lap.
            if (laps.size() == 1 && laps.first().endTime <= laps.first().startTime && !sectors.isEmpty())
                {
                    int last = -1;
                    double finish = laps.first().startTime;
                    for (int i = 0; i < sectors.size(); ++i)
                        {
                            const QVector<double> crossed = splits(*session, {sectors.at(i)}, laps.first().startTime, end);
                            if (!crossed.isEmpty() && crossed.first() > finish)
                                {
                                    finish = crossed.first();
                                    last = i;
                                }
                        }
                    if (last >= 0)
                        {
                            laps.first().endTime = finish;
                            sectors.removeAt(last);
                            openCourse = true;
                        }
                }
            // the line decides: whatever the file said about laps goes
            for (const char *id : kLapChannels)
                {
                    session->removeChannel(id);
                }
            // the lap under way at the end is not a finished one
            QVector<TelemetrySession::Lap> finished;
            for (const TelemetrySession::Lap &lap : laps)
                {
                    if (lap.endTime > lap.startTime)
                        {
                            finished.append(lap);
                        }
                }
            session->setLaps(finished);
        }
    if (laps.isEmpty())
        {
            addRunningFigures(*session, {{0.0, end}});
            return session;
        }
    std::sort(laps.begin(), laps.end(), [](const TelemetrySession::Lap &a, const TelemetrySession::Lap &b)
    {
        return a.startTime < b.startTime;
    });
    // the stretch after the last crossing is a lap under way, to the end
    QVector<TelemetrySession::Lap> all;
    for (const TelemetrySession::Lap &lap : laps)
        {
            if (lap.endTime > lap.startTime)
                {
                    all.append(lap);
                }
        }
    const double lastStart = all.isEmpty() ? laps.last().startTime : all.last().endTime;
    const bool underWay = line.isValid() && !openCourse && end - lastStart > 1.0;
    const int finished = int(all.size());
    if (!sectors.isEmpty())
        {
            for (TelemetrySession::Lap &lap : all)
                {
                    lap.splits = splits(*session, sectors, lap.startTime, lap.endTime);
                    // a lap that missed a line has no sector times
                    if (lap.splits.size() != sectors.size())
                        {
                            lap.splits.clear();
                        }
                }
            session->setLaps(all);
        }

    auto add = [&](const char *id, const QString &unit, Channel::Interpolation interpolation) -> Channel *
    {
        if (has(*session, id))
            {
                return nullptr;
            }
        session->addDerivedChannel(id);
        return &session->channelForWriting(id, unit, interpolation);
    };
    if (Channel *number = add(ChannelId::Lap, QString(), Channel::Step))
        {
            for (int i = 0; i < finished; ++i)
                {
                    number->append(all.at(i).startTime, i + 1);
                }
            if (underWay)
                {
                    number->append(lastStart, finished + 1);
                }
        }
    if (Channel *count = add(ChannelId::LapCount, QString(), Channel::Step))
        {
            count->append(0.0, finished);
            count->append(std::max(end, 1.0), finished);
        }
    if (Channel *running = add(ChannelId::LapTime, "s", Channel::Linear))
        {
            // counts up through a lap and starts again at 0 on the line
            for (const TelemetrySession::Lap &lap : all)
                {
                    running->append(lap.startTime, 0.0);
                    running->append(lap.endTime - 0.001, lap.endTime - lap.startTime - 0.001);
                }
            if (underWay)
                {
                    running->append(lastStart, 0.0);
                    running->append(end, end - lastStart);
                }
        }
    Channel *previous = add(ChannelId::PreviousLapTime, "s", Channel::Step);
    Channel *best = add(ChannelId::BestLapTime, "s", Channel::Step);
    Channel *bestNumber = add(ChannelId::BestLapNumber, QString(), Channel::Step);
    double fastest = std::numeric_limits<double>::max();
    for (int i = 0; i < finished; ++i)
        {
            const TelemetrySession::Lap &lap = all.at(i);
            const double duration = lap.endTime - lap.startTime;
            if (duration < fastest && bestNumber)
                {
                    bestNumber->append(lap.endTime, i + 1);
                }
            fastest = std::min(fastest, duration);
            if (previous)
                {
                    previous->append(lap.endTime, duration);
                }
            if (best)
                {
                    best->append(lap.endTime, fastest);
                }
        }

    // delta: how much later than the best lap so far the route reaches the same point of the lap
    const Channel *distance = session->channel(ChannelId::Distance);
    if (distance && !distance->isEmpty() && finished >= 1)
        {
            if (Channel *delta = add(ChannelId::LapDelta, "s", Channel::Linear))
                {
                    QVector<TelemetrySession::Lap> timed = all;
                    if (underWay)
                        {
                            timed.append({lastStart, end});
                        }
                    int bestIndex = -1;
                    QVector<QPointF> bestProfile;
                    double bestLength = 0.0;
                    for (int k = 0; k < timed.size(); ++k)
                        {
                            const TelemetrySession::Lap &lap = timed.at(k);
                            const double d0 = distance->valueAt(lap.startTime);
                            const bool complete = k < finished;
                            if (bestIndex >= 0)
                                {
                                    // an unfinished lap is measured against the best one's length
                                    const double length = complete ? distance->valueAt(lap.endTime) - d0 : bestLength;
                                    for (double t = lap.startTime; t < lap.endTime && length > 0.0; t += 0.5)
                                        {
                                            const double fraction = (distance->valueAt(t) - d0) / length;
                                            delta->append(t, (t - lap.startTime) - timeAt(bestProfile, fraction));
                                        }
                                }
                            const double duration = lap.endTime - lap.startTime;
                            if (complete && (bestIndex < 0 || duration < timed.at(bestIndex).endTime - timed.at(bestIndex).startTime))
                                {
                                    const double length = distance->valueAt(lap.endTime) - d0;
                                    if (length > 0.0)
                                        {
                                            bestIndex = k;
                                            bestLength = length;
                                            bestProfile = profile(*distance, lap.startTime, lap.endTime, length);
                                        }
                                }
                        }
                    if (delta->isEmpty())
                        {
                            session->removeChannel(ChannelId::LapDelta);
                        }
                }
        }

    // against the reference lap: the one chosen, or the fastest of the whole ride
    int referenceIndex = reference >= 1 && reference <= finished ? reference - 1 : -1;
    if (referenceIndex < 0)
        {
            for (int k = 0; k < finished; ++k)
                {
                    const double took = all.at(k).endTime - all.at(k).startTime;
                    if (referenceIndex < 0 || took < all.at(referenceIndex).endTime - all.at(referenceIndex).startTime)
                        {
                            referenceIndex = k;
                        }
                }
        }
    if (referenceIndex >= 0)
        {
            const TelemetrySession::Lap &chosen = all.at(referenceIndex);
            session->setReferenceLap(chosen);
            if (Channel *took = add(ChannelId::ReferenceLapTime, "s", Channel::Step))
                {
                    took->append(0.0, chosen.endTime - chosen.startTime);
                    took->append(std::max(end, 1.0), chosen.endTime - chosen.startTime);
                }
            const double d0 = distance ? distance->valueAt(chosen.startTime) : 0.0;
            const double length = distance ? distance->valueAt(chosen.endTime) - d0 : 0.0;
            Channel *against = length > 0.0 ? add(ChannelId::ReferenceDelta, "s", Channel::Linear) : nullptr;
            if (against)
                {
                    const QVector<QPointF> referenceProfile = profile(*distance, chosen.startTime, chosen.endTime, length);
                    QVector<TelemetrySession::Lap> timed = all;
                    if (underWay)
                        {
                            timed.append({lastStart, end});
                        }
                    for (int k = 0; k < timed.size(); ++k)
                        {
                            const TelemetrySession::Lap &lap = timed.at(k);
                            const double start = distance->valueAt(lap.startTime);
                            // an unfinished lap is measured against the reference's length
                            const double lapLength = k < finished ? distance->valueAt(lap.endTime) - start : length;
                            for (double t = lap.startTime; t < lap.endTime && lapLength > 0.0; t += 0.5)
                                {
                                    const double fraction = (distance->valueAt(t) - start) / lapLength;
                                    against->append(t, (t - lap.startTime) - timeAt(referenceProfile, fraction));
                                }
                        }
                }
        }
    QVector<QPair<double, double>> stretches;
    if (!all.isEmpty() && all.first().startTime > 1.0)
        {
            stretches.append({0.0, all.first().startTime});   // the way to the line
        }
    for (const TelemetrySession::Lap &lap : std::as_const(all))
        {
            stretches.append({lap.startTime, lap.endTime});
        }
    if (underWay || all.isEmpty())
        {
            stretches.append({lastStart, end});
        }
    addRunningFigures(*session, stretches);
    session->removeEmptyChannels();
    if (!sectors.isEmpty())
        {
            addSectorChannels(*session, all, underWay ? splits(*session, sectors, lastStart, end) : QVector<double>(),
                              lastStart, underWay ? end : -1.0, int(sectors.size()) + 1);
        }
    return session;
}
