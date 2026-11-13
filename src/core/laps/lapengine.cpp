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
    ChannelId::LapDelta, ChannelId::LapCount,
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
}

QVector<TelemetrySession::Lap> LapEngine::detect(const TelemetrySession &session, const StartFinishLine &line,
        double minimumLap)
{
    QVector<TelemetrySession::Lap> laps;
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    if (!line.isValid() || !lat || !lon || lat->count() < 2)
        {
            return laps;
        }
    const Plane plane((line.lat1 + line.lat2) / 2.0, (line.lon1 + line.lon2) / 2.0);
    const QPointF a = plane.toPlane(line.lat1, line.lon1);
    const QPointF ab = plane.toPlane(line.lat2, line.lon2) - a;

    QVector<double> crossings;
    int direction = 0;
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
                    const int side = denominator > 0.0 ? 1 : -1;
                    if (u >= 0.0 && u < 1.0 && v >= 0.0 && v <= 1.0 && (direction == 0 || side == direction))
                        {
                            const double t = t0 + u * (t1 - t0);
                            if (crossings.isEmpty() || t - crossings.last() >= minimumLap)
                                {
                                    direction = side;
                                    crossings.append(t);
                                }
                        }
                }
            previous = p;
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

TelemetrySessionPtr LapEngine::apply(const TelemetrySessionPtr &base, const StartFinishLine &line)
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
    return session;
}
