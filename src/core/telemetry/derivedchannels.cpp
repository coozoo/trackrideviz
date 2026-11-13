#include "derivedchannels.h"

#include "channelids.h"
#include "geo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.14159265358979323846;
const double kGravity = 9.80665;
// slower than this, heading, grade and sideways g mean nothing
const double kMoving = 1.0;              // m/s
// grade over this much distance on either side
const double kGradeHalfDistance = 20.0;  // m
// elevation changes smaller than this are noise, not climbing
const double kClimbThreshold = 2.0;      // m

struct Grid
{
    QVector<double> t;
    QVector<bool> valid;       // not inside a pause
    double step{1.0};

    int count() const { return int(t.size()); }
    // grid points in `seconds`, at least one
    int span(double seconds) const { return std::max(1, int(std::lround(seconds / step))); }
};

Grid makeGrid(const TelemetrySession &session, const Channel &base)
{
    Grid grid;
    QVector<double> gaps;
    for (int i = 1; i < base.count(); ++i)
        {
            gaps.append(base.timeAt(i) - base.timeAt(i - 1));
        }
    if (!gaps.isEmpty())
        {
            std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
            grid.step = std::clamp(gaps.at(gaps.size() / 2), 0.1, 1.0);
        }
    const double start = base.startTime();
    const int count = int((base.endTime() - start) / grid.step) + 1;
    for (int i = 0; i < count; ++i)
        {
            const double t = start + i * grid.step;
            bool paused = false;
            for (const TelemetrySession::Pause &p : session.pauses())
                {
                    paused = paused || (t > p.startTime && t < p.endTime);
                }
            grid.t.append(t);
            grid.valid.append(!paused);
        }
    return grid;
}

QVector<double> sample(const Channel *channel, const Grid &grid)
{
    QVector<double> values(grid.count(), kNaN);
    if (!channel)
        {
            return values;
        }
    for (int i = 0; i < grid.count(); ++i)
        {
            bool ok = false;
            const double v = channel->valueAt(grid.t.at(i), &ok);
            if (ok && grid.valid.at(i))
                {
                    values[i] = v;
                }
        }
    return values;
}

// Centred mean over `half` points either side, of the values there are.
QVector<double> smooth(const QVector<double> &values, int half)
{
    QVector<double> result(values.size(), kNaN);
    double sum = 0.0;
    int count = 0;
    const int n = int(values.size());
    // running sum over [i - half, i + half]
    for (int i = -half; i < n + half; ++i)
        {
            const int in = i + half;
            if (in < n && std::isfinite(values.at(in)))
                {
                    sum += values.at(in);
                    ++count;
                }
            const int out = i - half - 1;
            if (out >= 0 && out < n && std::isfinite(values.at(out)))
                {
                    sum -= values.at(out);
                    --count;
                }
            if (i >= 0 && i < n && std::isfinite(values.at(i)) && count > 0)
                {
                    result[i] = sum / count;
                }
        }
    return result;
}

// The change per second over `k` points either side.
double slope(const QVector<double> &values, const Grid &grid, int i, int k)
{
    const int a = std::max(0, i - k);
    const int b = std::min(grid.count() - 1, i + k);
    if (b <= a || !std::isfinite(values.at(a)) || !std::isfinite(values.at(b)))
        {
            return kNaN;
        }
    return (values.at(b) - values.at(a)) / (grid.t.at(b) - grid.t.at(a));
}

void store(TelemetrySession &session, const QString &id, const QString &unit, const Grid &grid,
           const QVector<double> &values)
{
    Channel &channel = session.channelForWriting(id, unit);
    for (int i = 0; i < grid.count(); ++i)
        {
            if (std::isfinite(values.at(i)))
                {
                    channel.append(grid.t.at(i), values.at(i));
                }
        }
    if (channel.isEmpty())
        {
            session.removeEmptyChannels();
        }
    else
        {
            session.addDerivedChannel(id);
        }
}

bool has(const TelemetrySession &session, const char *id)
{
    const Channel *channel = session.channel(id);
    return channel && !channel->isEmpty();
}

// Clockwise from north, 0..360.
double bearing(double lat1, double lon1, double lat2, double lon2)
{
    const double p1 = lat1 * kPi / 180.0;
    const double p2 = lat2 * kPi / 180.0;
    const double dl = (lon2 - lon1) * kPi / 180.0;
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    const double b = std::atan2(y, x) * 180.0 / kPi;
    return b < 0.0 ? b + 360.0 : b;
}
}

TelemetrySessionPtr DerivedChannels::complete(const TelemetrySessionPtr &original)
{
    if (!original)
        {
            return original;
        }
    auto session = std::make_shared<TelemetrySession>(*original);
    const Channel *lat = session->channel(ChannelId::PositionLat);
    const Channel *lon = session->channel(ChannelId::PositionLon);
    const bool positions = lat && lon && lat->count() >= 2 && lon->count() >= 2;

    // distance along the positions as recorded
    if (positions && !has(*session, ChannelId::Distance))
        {
            Channel &distance = session->channelForWriting(ChannelId::Distance, "m");
            double total = 0.0;
            double previousLat = lat->valueAtIndex(0);
            double previousLon = lon->valueAt(lat->timeAt(0));
            distance.append(lat->timeAt(0), 0.0);
            for (int i = 1; i < lat->count(); ++i)
                {
                    const double t = lat->timeAt(i);
                    const double la = lat->valueAtIndex(i);
                    const double lo = lon->valueAt(t);
                    const double step = Geo::distanceMeters(previousLat, previousLon, la, lo);
                    // a position glitch is not a journey: nothing on wheels covers 100 m/s
                    if (step <= 100.0 * std::max(t - lat->timeAt(i - 1), 1.0))
                        {
                            total += step;
                        }
                    previousLat = la;
                    previousLon = lo;
                    distance.append(t, total);
                }
            session->addDerivedChannel(ChannelId::Distance);
        }

    const Channel *base = positions ? lat : session->channel(ChannelId::Speed);
    if (!base || base->count() < 2)
        {
            base = session->channel(ChannelId::Elevation);
        }
    if (!base || base->count() < 2)
        {
            return session->derivedChannels().isEmpty() ? original : session;
        }
    const Grid grid = makeGrid(*session, *base);
    const int n = grid.count();
    const QVector<double> distance = sample(session->channel(ChannelId::Distance), grid);

    if (!has(*session, ChannelId::Speed) && has(*session, ChannelId::Distance))
        {
            QVector<double> speed(n, kNaN);
            for (int i = 0; i < n; ++i)
                {
                    speed[i] = std::max(0.0, slope(distance, grid, i, grid.span(1.0)));
                }
            store(*session, ChannelId::Speed, "m/s", grid, speed);
        }
    const QVector<double> speed = sample(session->channel(ChannelId::Speed), grid);

    if (positions && !has(*session, ChannelId::Heading))
        {
            // between points a moment apart, held while standing still
            QVector<double> heading(n, kNaN);
            double held = kNaN;
            const int k = grid.span(1.0);
            for (int i = 0; i < n; ++i)
                {
                    const int a = std::max(0, i - k);
                    const int b = std::min(n - 1, i + k);
                    bool okA = false;
                    bool okB = false;
                    const double la1 = lat->valueAt(grid.t.at(a), &okA);
                    const double la2 = lat->valueAt(grid.t.at(b), &okB);
                    const double lo1 = lon->valueAt(grid.t.at(a));
                    const double lo2 = lon->valueAt(grid.t.at(b));
                    if (okA && okB && grid.valid.at(i) && std::isfinite(speed.at(i)) && speed.at(i) >= kMoving
                            && Geo::distanceMeters(la1, lo1, la2, lo2) > 1.0)
                        {
                            held = bearing(la1, lo1, la2, lo2);
                        }
                    if (grid.valid.at(i))
                        {
                            heading[i] = held;
                        }
                }
            store(*session, ChannelId::Heading, "deg", grid, heading);
        }

    if (has(*session, ChannelId::Elevation))
        {
            const QVector<double> elevation = smooth(sample(session->channel(ChannelId::Elevation), grid), grid.span(2.5));
            if (!has(*session, ChannelId::Grade) && has(*session, ChannelId::Distance))
                {
                    // rise over the distance around the point, not over one noisy step
                    QVector<double> grade(n, kNaN);
                    const int reach = grid.span(60.0);
                    for (int i = 0; i < n; ++i)
                        {
                            if (!std::isfinite(distance.at(i)) || !std::isfinite(speed.at(i)) || speed.at(i) < kMoving)
                                {
                                    continue;
                                }
                            int a = i;
                            while (a > 0 && i - a < reach && distance.at(i) - distance.at(a) < kGradeHalfDistance)
                                {
                                    --a;
                                }
                            int b = i;
                            while (b < n - 1 && b - i < reach && distance.at(b) - distance.at(i) < kGradeHalfDistance)
                                {
                                    ++b;
                                }
                            const double run = distance.at(b) - distance.at(a);
                            if (run >= kGradeHalfDistance && std::isfinite(elevation.at(a)) && std::isfinite(elevation.at(b)))
                                {
                                    grade[i] = std::clamp((elevation.at(b) - elevation.at(a)) / run * 100.0, -40.0, 40.0);
                                }
                        }
                    store(*session, ChannelId::Grade, "%", grid, grade);
                }
            if (!has(*session, ChannelId::GradeAngle) && has(*session, ChannelId::Grade))
                {
                    QVector<double> angle = sample(session->channel(ChannelId::Grade), grid);
                    for (double &v : angle)
                        {
                            v = std::atan(v / 100.0) * 180.0 / kPi;
                        }
                    store(*session, ChannelId::GradeAngle, "deg", grid, angle);
                }
            if (!has(*session, ChannelId::TotalAscent) && !has(*session, ChannelId::TotalDescent))
                {
                    QVector<double> ascent(n, kNaN);
                    QVector<double> descent(n, kNaN);
                    double up = 0.0;
                    double down = 0.0;
                    double reference = kNaN;
                    for (int i = 0; i < n; ++i)
                        {
                            const double e = elevation.at(i);
                            if (std::isfinite(e))
                                {
                                    if (!std::isfinite(reference))
                                        {
                                            reference = e;
                                        }
                                    else if (e - reference >= kClimbThreshold)
                                        {
                                            up += e - reference;
                                            reference = e;
                                        }
                                    else if (reference - e >= kClimbThreshold)
                                        {
                                            down += reference - e;
                                            reference = e;
                                        }
                                }
                            if (std::isfinite(reference) && grid.valid.at(i))
                                {
                                    ascent[i] = up;
                                    descent[i] = down;
                                }
                        }
                    store(*session, ChannelId::TotalAscent, "m", grid, ascent);
                    store(*session, ChannelId::TotalDescent, "m", grid, descent);
                }
            if (!has(*session, ChannelId::VerticalSpeed))
                {
                    QVector<double> vertical(n, kNaN);
                    for (int i = 0; i < n; ++i)
                        {
                            vertical[i] = slope(elevation, grid, i, grid.span(2.0));
                        }
                    store(*session, ChannelId::VerticalSpeed, "m/s", grid, vertical);
                }
        }

    // G-force: forwards from the change of speed, sideways from speed times turn rate
    if (!has(*session, ChannelId::GForceLateral) && !has(*session, ChannelId::GForceLongitudinal)
            && has(*session, ChannelId::Speed))
        {
            const int half = std::max(grid.span(1.0), 1);
            const QVector<double> smoothSpeed = smooth(speed, half);
            QVector<double> turn = sample(session->channel(ChannelId::Heading), grid);
            // one unbroken angle, so 359 -> 1 is 2 degrees, not -358
            double offset = 0.0;
            double previous = kNaN;
            for (double &h : turn)
                {
                    if (!std::isfinite(h))
                        {
                            continue;
                        }
                    if (std::isfinite(previous))
                        {
                            const double d = h + offset - previous;
                            offset -= 360.0 * std::round(d / 360.0);
                        }
                    h += offset;
                    previous = h;
                }
            turn = smooth(turn, half);
            QVector<double> forwards(n, kNaN);
            QVector<double> sideways(n, kNaN);
            for (int i = 0; i < n; ++i)
                {
                    if (!grid.valid.at(i) || !std::isfinite(smoothSpeed.at(i)))
                        {
                            continue;
                        }
                    forwards[i] = std::clamp(slope(smoothSpeed, grid, i, half) / kGravity, -3.0, 3.0);
                    const double rate = slope(turn, grid, i, half) * kPi / 180.0;
                    sideways[i] = smoothSpeed.at(i) < kMoving || !std::isfinite(rate)
                                  ? 0.0 : std::clamp(smoothSpeed.at(i) * rate / kGravity, -3.0, 3.0);
                }
            store(*session, ChannelId::GForceLongitudinal, "g", grid, forwards);
            store(*session, ChannelId::GForceLateral, "g", grid, sideways);
        }
    return session->derivedChannels().isEmpty() ? original : session;
}
