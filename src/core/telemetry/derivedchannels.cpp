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

// Centerd mean over `half` points either side, of the values there are.
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

// The mean of the values in the `points` up to and including each one.
QVector<double> trailingMean(const QVector<double> &values, int points)
{
    QVector<double> result(values.size(), kNaN);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < values.size(); ++i)
        {
            if (std::isfinite(values.at(i)))
                {
                    sum += values.at(i);
                    ++count;
                }
            const int out = i - points;
            if (out >= 0 && std::isfinite(values.at(out)))
                {
                    sum -= values.at(out);
                    --count;
                }
            if (std::isfinite(values.at(i)) && count > 0)
                {
                    result[i] = sum / count;
                }
        }
    return result;
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

// --- a camera's motion sensors ---------------------------------------------------------

struct Vec3
{
    double x{0.0};
    double y{0.0};
    double z{0.0};

    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3 &o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

QVector<Vec3> sample3(const TelemetrySession &session, const char *x, const char *y, const char *z, const Grid &grid,
                      int half)
{
    const QVector<double> xs = smooth(sample(session.channel(x), grid), half);
    const QVector<double> ys = smooth(sample(session.channel(y), grid), half);
    const QVector<double> zs = smooth(sample(session.channel(z), grid), half);
    QVector<Vec3> values(grid.count());
    for (int i = 0; i < grid.count(); ++i)
        {
            values[i] = {xs.at(i), ys.at(i), zs.at(i)};
        }
    return values;
}

// How closely two series go together, -1..1; 0 for too few points.
double correlation(const QVector<double> &a, const QVector<double> &b)
{
    double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;
    int n = 0;
    for (int i = 0; i < a.size(); ++i)
        {
            if (std::isfinite(a.at(i)) && std::isfinite(b.at(i)))
                {
                    sa += a.at(i);
                    sb += b.at(i);
                    saa += a.at(i) * a.at(i);
                    sbb += b.at(i) * b.at(i);
                    sab += a.at(i) * b.at(i);
                    ++n;
                }
        }
    if (n < 20)
        {
            return 0.0;
        }
    const double cov = sab - sa * sb / n;
    const double va = saa - sa * sa / n;
    const double vb = sbb - sb * sb / n;
    return va > 0.0 && vb > 0.0 ? cov / std::sqrt(va * vb) : 0.0;
}

struct Motion
{
    QVector<double> forwards;   // g
    QVector<double> sideways;   // g, + to the right
    QVector<double> vertical;   // g, 1 standing still
    QVector<double> lean;       // degrees, + to the right; empty without a gyroscope
};

// The G-forces of a camera's accelerometer, turned from the camera's axes to those of the
// vehicle; false when they cannot be lined up with the movement the GPS saw.
//
// The camera may be mounted any way, and camera models write the axes in different orders.
// Up is where gravity pulls from, on average over the ride. Forwards is the level direction
// whose acceleration goes best with the changes of speed; right is then across the two,
// and turned round when it goes against the turns.
//
// On a motorcycle or a bicycle the camera leans into the turns with the rider, and then
// feels hardly anything sideways: the pull of the turn and gravity add up along the leaning
// rider. A gyroscope tells: when the camera rolls as the turns ask for, the lean is its roll
// rate added up, drawn slowly towards the turn's lean so it does not drift, and the readings
// are turned back level by it.
bool fromMotionSensors(const TelemetrySession &session, const Grid &grid, const QVector<double> &gpsForwards,
                       const QVector<double> &gpsSideways, Motion *motion)
{
    if (!has(session, ChannelId::AccelX) || !has(session, ChannelId::AccelY) || !has(session, ChannelId::AccelZ))
        {
            return false;
        }
    const int n = grid.count();
    // the shaking of the road evened out over a quarter of a second; over a second like the
    // GPS's own for lining the two up
    const QVector<Vec3> accel = sample3(session, ChannelId::AccelX, ChannelId::AccelY, ChannelId::AccelZ, grid,
                                        grid.span(0.25));
    const QVector<Vec3> slow = sample3(session, ChannelId::AccelX, ChannelId::AccelY, ChannelId::AccelZ, grid,
                                       grid.span(1.0));
    Vec3 gravity;
    int count = 0;
    for (const Vec3 &a : accel)
        {
            if (a.finite())
                {
                    gravity = gravity + a;
                    ++count;
                }
        }
    if (count < 20)
        {
            return false;
        }
    gravity = gravity * (1.0 / count);
    // a sensor that does not feel the earth's pull is no accelerometer in m/s2
    if (std::fabs(gravity.length() - kGravity) > 0.3 * kGravity)
        {
            return false;
        }
    const Vec3 up = gravity * (1.0 / gravity.length());
    auto level = [&up](const Vec3 &a)
    {
        return a - up * a.dot(up);
    };
    Vec3 ahead;
    for (int i = 0; i < n; ++i)
        {
            if (slow.at(i).finite() && std::isfinite(gpsForwards.at(i)))
                {
                    ahead = ahead + level(slow.at(i)) * gpsForwards.at(i);
                }
        }
    if (ahead.length() <= 0.0)
        {
            return false;
        }
    ahead = ahead * (1.0 / ahead.length());
    QVector<double> along(n, kNaN);
    for (int i = 0; i < n; ++i)
        {
            if (slow.at(i).finite())
                {
                    along[i] = slow.at(i).dot(ahead);
                }
        }
    // too little going on, or nothing to do with the ride
    if (correlation(along, gpsForwards) < 0.5)
        {
            return false;
        }

    // the lean the turns ask for, and whether the camera takes it
    QVector<double> asked(n, kNaN);
    for (int i = 0; i < n; ++i)
        {
            if (std::isfinite(gpsSideways.at(i)))
                {
                    asked[i] = std::atan(gpsSideways.at(i)) * 180.0 / kPi;
                }
        }
    QVector<double> lean;
    if (has(session, ChannelId::GyroX) && has(session, ChannelId::GyroY) && has(session, ChannelId::GyroZ))
        {
            // turning about the way ahead lifts up towards the right. A projection: true
            // whatever order and mirroring the camera's axes have
            const QVector<Vec3> gyro = sample3(session, ChannelId::GyroX, ChannelId::GyroY, ChannelId::GyroZ, grid, 0);
            QVector<double> roll(n, kNaN);
            for (int i = 0; i < n; ++i)
                {
                    if (gyro.at(i).finite())
                        {
                            roll[i] = gyro.at(i).dot(ahead);
                        }
                }
            const int k = grid.span(0.5);
            QVector<double> askedRate(n, kNaN);
            for (int i = 0; i < n; ++i)
                {
                    askedRate[i] = slope(asked, grid, i, k);
                }
            if (correlation(smooth(roll, k), askedRate) > 0.5)
                {
                    constexpr double kSettle = 2.0;  // s: how slowly the lean is drawn to the turn's
                    const double keep = kSettle / (kSettle + grid.step);
                    lean = QVector<double>(n, kNaN);
                    double angle = kNaN;
                    for (int i = 0; i < n; ++i)
                        {
                            if (!std::isfinite(roll.at(i)) || !grid.valid.at(i))
                                {
                                    angle = asked.at(i);
                                    continue;
                                }
                            if (!std::isfinite(angle))
                                {
                                    angle = std::isfinite(asked.at(i)) ? asked.at(i) : 0.0;
                                }
                            angle += roll.at(i) * grid.step;
                            if (std::isfinite(asked.at(i)))
                                {
                                    angle = keep * angle + (1.0 - keep) * asked.at(i);
                                }
                            angle = std::clamp(angle, -80.0, 80.0);
                            lean[i] = angle;
                        }
                }
        }

    // sideways and up in level axes: a leaning camera's readings turned back by its lean
    Vec3 right = ahead.cross(up);
    auto levelled = [&](const Vec3 &a, int i, double *sideways, double *upwards)
    {
        const double r = a.dot(right);
        const double u = a.dot(up);
        const double phi = lean.isEmpty() || !std::isfinite(lean.at(i)) ? 0.0 : lean.at(i) * kPi / 180.0;
        *sideways = r * std::cos(phi) + u * std::sin(phi);
        *upwards = u * std::cos(phi) - r * std::sin(phi);
    };
    QVector<double> across(n, kNaN);
    for (int i = 0; i < n; ++i)
        {
            double upwards = 0.0;
            if (slow.at(i).finite())
                {
                    levelled(slow.at(i), i, &across[i], &upwards);
                }
        }
    // the axes the camera writes may be mirrored: the turns say which side is right
    if (correlation(across, gpsSideways) < 0.0)
        {
            right = right * -1.0;
        }
    motion->forwards = QVector<double>(n, kNaN);
    motion->sideways = QVector<double>(n, kNaN);
    motion->vertical = QVector<double>(n, kNaN);
    for (int i = 0; i < n; ++i)
        {
            if (accel.at(i).finite() && grid.valid.at(i))
                {
                    double sideways = 0.0;
                    double upwards = 0.0;
                    levelled(accel.at(i), i, &sideways, &upwards);
                    motion->forwards[i] = std::clamp(accel.at(i).dot(ahead) / kGravity, -5.0, 5.0);
                    motion->sideways[i] = std::clamp(sideways / kGravity, -5.0, 5.0);
                    motion->vertical[i] = std::clamp(upwards / kGravity, -5.0, 5.0);
                }
        }
    motion->lean = lean;
    return true;
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
            Motion motion;
            if (fromMotionSensors(*session, grid, forwards, sideways, &motion))
                {
                    store(*session, ChannelId::GForceLongitudinal, "g", grid, motion.forwards);
                    store(*session, ChannelId::GForceLateral, "g", grid, motion.sideways);
                    if (!has(*session, ChannelId::GForceVertical))
                        {
                            store(*session, ChannelId::GForceVertical, "g", grid, motion.vertical);
                        }
                    if (!motion.lean.isEmpty() && !has(*session, ChannelId::LeanAngle))
                        {
                            store(*session, ChannelId::LeanAngle, "deg", grid, motion.lean);
                        }
                }
            else
                {
                    store(*session, ChannelId::GForceLongitudinal, "g", grid, forwards);
                    store(*session, ChannelId::GForceLateral, "g", grid, sideways);
                }
        }

    // the small sums
    auto same = [&](const char *id, const QString &unit, double value)
    {
        // one value from the first moment to the last
        Channel &channel = session->channelForWriting(id, unit, Channel::Step);
        channel.append(grid.t.first(), value);
        channel.append(std::max(grid.t.last(), grid.t.first() + 1.0), value);
        session->addDerivedChannel(id);
    };
    if (!has(*session, ChannelId::ElapsedTime))
        {
            Channel &elapsed = session->channelForWriting(ChannelId::ElapsedTime, "s");
            elapsed.append(grid.t.first(), grid.t.first());
            elapsed.append(std::max(grid.t.last(), grid.t.first() + 1.0), std::max(grid.t.last(), grid.t.first() + 1.0));
            session->addDerivedChannel(ChannelId::ElapsedTime);
        }
    if (!has(*session, ChannelId::Pace) && has(*session, ChannelId::Speed))
        {
            // of the speed over a few seconds: a pace jumps about more than the speed it comes from
            const QVector<double> steady = smooth(speed, grid.span(2.5));
            QVector<double> pace(n, kNaN);
            for (int i = 0; i < n; ++i)
                {
                    if (std::isfinite(steady.at(i)) && steady.at(i) >= 0.5)
                        {
                            pace[i] = 1000.0 / steady.at(i);
                        }
                }
            store(*session, ChannelId::Pace, "s/km", grid, pace);
        }
    if (has(*session, ChannelId::Power))
        {
            const QVector<double> power = sample(session->channel(ChannelId::Power), grid);
            if (!has(*session, ChannelId::Power3s))
                {
                    store(*session, ChannelId::Power3s, "W", grid, trailingMean(power, grid.span(3.0)));
                }
            if (!has(*session, ChannelId::Power30s))
                {
                    store(*session, ChannelId::Power30s, "W", grid, trailingMean(power, grid.span(30.0)));
                }
        }
    if (has(*session, ChannelId::Elevation))
        {
            if (!has(*session, ChannelId::ElevationMax))
                {
                    same(ChannelId::ElevationMax, "m", session->channel(ChannelId::Elevation)->maxValue());
                }
            if (!has(*session, ChannelId::ElevationMin))
                {
                    same(ChannelId::ElevationMin, "m", session->channel(ChannelId::Elevation)->minValue());
                }
        }
    if (has(*session, ChannelId::Distance) && !has(*session, ChannelId::DistanceTotal))
        {
            same(ChannelId::DistanceTotal, "m", session->channel(ChannelId::Distance)->maxValue());
        }
    if (has(*session, ChannelId::GForceLongitudinal) && !has(*session, ChannelId::Braking))
        {
            QVector<double> braking = sample(session->channel(ChannelId::GForceLongitudinal), grid);
            for (double &g : braking)
                {
                    g = std::isfinite(g) ? std::max(0.0, -g) : g;
                }
            store(*session, ChannelId::Braking, "g", grid, braking);
        }
    if (has(*session, ChannelId::VerticalSpeed))
        {
            const QVector<double> vertical = sample(session->channel(ChannelId::VerticalSpeed), grid);
            if (!has(*session, ChannelId::FallingSpeed))
                {
                    QVector<double> falling(vertical);
                    for (double &v : falling)
                        {
                            v = std::isfinite(v) ? std::max(0.0, -v) : v;
                        }
                    store(*session, ChannelId::FallingSpeed, "m/s", grid, falling);
                }
            if (!has(*session, ChannelId::GlideRatio) && has(*session, ChannelId::Speed))
                {
                    QVector<double> glide(n, kNaN);
                    for (int i = 0; i < n; ++i)
                        {
                            // only while really going down: level flight has no ratio
                            if (std::isfinite(vertical.at(i)) && std::isfinite(speed.at(i)) && vertical.at(i) <= -0.5)
                                {
                                    glide[i] = std::min(speed.at(i) / -vertical.at(i), 99.0);
                                }
                        }
                    store(*session, ChannelId::GlideRatio, QString(), grid, glide);
                }
        }
    return session->derivedChannels().isEmpty() ? original : session;
}
