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
const char *const kLapChannels[] =
{
    ChannelId::Lap, ChannelId::LapTime, ChannelId::PreviousLapTime, ChannelId::BestLapTime,
    ChannelId::LapDelta, ChannelId::LapCount, ChannelId::Sector, ChannelId::SectorTime, ChannelId::PreviousSectorTime,
    ChannelId::SectorDelta,
};

// Metres east and north of a reference point; good for a few kilometres.
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
    for (double t = start; t <= end; t += 0.25)
        {
            points.append(QPointF((distance.valueAt(t) - d0) / length, t - start));
        }
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

TelemetrySessionPtr LapEngine::apply(const TelemetrySessionPtr &base, const StartFinishLine &line,
                                     const QVector<StartFinishLine> &sectors)
{
    if (!base)
        {
            return base;
        }
    auto session = std::make_shared<TelemetrySession>(*base);
    double end = session->duration();
    QVector<TelemetrySession::Lap> laps = session->laps();
    if (line.isValid())
        {
            laps = detect(*session, line);
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
            return line.isValid() ? TelemetrySessionPtr(session) : base;
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
    const bool underWay = line.isValid() && end - lastStart > 1.0;
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
    double fastest = std::numeric_limits<double>::max();
    for (const TelemetrySession::Lap &lap : all)
        {
            const double duration = lap.endTime - lap.startTime;
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
    if (!sectors.isEmpty())
        {
            addSectorChannels(*session, all, underWay ? splits(*session, sectors, lastStart, end) : QVector<double>(),
                              lastStart, underWay ? end : -1.0, int(sectors.size()) + 1);
        }
    return session;
}
