#include "powerestimate.h"

#include "channelids.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double kGravity = 9.80665;
const double kPi = 3.14159265358979323846;
// air at sea level and 15 degrees, kg/m3, and the height over which it thins e-fold
const double kSeaLevelDensity = 1.225;
const double kScaleHeight = 8434.5;   // m
const double kStandardKelvin = 288.15;
// the build the drag areas below were measured for
const double kReferenceKg = 75.0;
const double kReferenceCm = 178.0;
// Speed is smoothed over this much on either side, its change taken over as much, and the
// power smoothed again. A speed worked out from positions needs the longer: positions jitter
// and are often stored only every few seconds, the speed between them then comes in steps,
// and what a gain of speed seems to ask for swings both ways from second to second. A speed
// the logger measured follows the legs closely enough for the shorter.
const int kSmoothRecorded = 1;   // s: three seconds in all
const int kSmoothWorkedOut = 5;

struct Kind
{
    const char *id;
    double value;
};

// On typical asphalt, as measured on test drums and scaled to the road.
const Kind kTires[] =
{
    {"road-fast", 0.0035}, {"road-typical", 0.005}, {"road-slow", 0.007},
    {"gravel-fast", 0.0055}, {"gravel-typical", 0.0075}, {"gravel-slow", 0.010},
    {"mtb-fast", 0.008}, {"mtb-typical", 0.012}, {"mtb-slow", 0.018},
};

// Drag area, rider and bike together, m2.
const Kind kPositions[] =
{
    {"tops", 0.41}, {"hoods", 0.36}, {"drops", 0.31}, {"aerobars", 0.25}, {"upright", 0.55},
};

template <typename Kinds>
double valueOf(const Kinds &kinds, const QString &id)
{
    for (const Kind &kind : kinds)
        {
            if (id == QLatin1String(kind.id))
                {
                    return kind.value;
                }
        }
    return 0.0;
}

// The mean of the finite values within `half` points on either side.
QVector<double> smooth(const QVector<double> &values, int half)
{
    QVector<double> result(values.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < values.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;
            for (int k = std::max(0, i - half); k <= std::min(int(values.size()) - 1, i + half); ++k)
                {
                    if (std::isfinite(values.at(k)))
                        {
                            sum += values.at(k);
                            ++count;
                        }
                }
            if (count > 0 && std::isfinite(values.at(i)))
                {
                    result[i] = sum / count;
                }
        }
    return result;
}
}

QStringList PowerEstimate::tires()
{
    QStringList ids;
    for (const Kind &kind : kTires)
        {
            ids << kind.id;
        }
    return ids;
}

QStringList PowerEstimate::positions()
{
    QStringList ids;
    for (const Kind &kind : kPositions)
        {
            ids << kind.id;
        }
    return ids;
}

double PowerEstimate::crrOf(const QString &tires)
{
    return valueOf(kTires, tires);
}

double PowerEstimate::cdaOf(const QString &position, double riderKg, double heightCm)
{
    // the front a body shows grows as its surface does (Du Bois)
    const double build = std::pow(std::max(riderKg, 30.0) / kReferenceKg, 0.425)
                         * std::pow(std::max(heightCm, 100.0) / kReferenceCm, 0.725);
    return valueOf(kPositions, position) * build;
}

double PowerEstimate::crr(const Rider &rider)
{
    const double known = crrOf(rider.tires);
    return known > 0.0 ? known : rider.crr;
}

double PowerEstimate::cda(const Rider &rider)
{
    const double known = cdaOf(rider.position, rider.riderKg, rider.heightCm);
    return known > 0.0 ? known : rider.cda;
}

QString PowerEstimate::tireKind(const QString &size)
{
    // "700x25c", "26 x 1.95", "29x2.25": the width follows the x; "40-622": it leads
    static const QRegularExpression crossed(R"(\d+(?:[.,]\d+)?\s*[x×]\s*(\d+(?:[.,]\d+)?))",
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression etrto(R"(^\s*(\d{2,3})\s*-\s*\d{3}\s*$)");
    QRegularExpressionMatch match = crossed.match(size);
    if (!match.hasMatch())
        {
            match = etrto.match(size);
        }
    if (!match.hasMatch())
        {
            return QString();
        }
    double width = match.captured(1).replace(',', '.').toDouble();
    if (width <= 0.0)
        {
            return QString();
        }
    if (width < 10.0)
        {
            width *= 25.4;   // inches
        }
    return width < 33.0 ? "road" : width < 48.0 ? "gravel" : "mtb";
}

TelemetrySessionPtr PowerEstimate::apply(const TelemetrySessionPtr &original, const Rider &rider, const Weather &weather)
{
    if (!original || !rider.enabled)
        {
            return original;
        }
    const Channel *recorded = original->channel(ChannelId::Power);
    const Channel *speed = original->channel(ChannelId::Speed);
    if (!speed || speed->count() < 2)
        {
            return original;
        }
    const Channel *grade = original->channel(ChannelId::Grade);
    const Channel *elevation = original->channel(ChannelId::Elevation);
    const Channel *temperature = original->channel(ChannelId::Temperature);
    const Channel *cadence = original->channel(ChannelId::Cadence);
    const Channel *heading = original->channel(ChannelId::Heading);
    const Channel *distance = original->channel(ChannelId::Distance);
    const bool windKnown = !weather.isEmpty() && heading && !heading->isEmpty() && distance && !distance->isEmpty()
                           && original->startUtc().isValid();
    const qint64 startUtc = windKnown ? original->startUtc().toSecsSinceEpoch() : 0;
    const double reaching = std::clamp(rider.windShare, 0.0, 100.0) / 100.0;

    // one value a second, not inside pauses
    const double start = speed->startTime();
    const int count = int(speed->endTime() - start) + 1;
    QVector<double> v(count, std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < count; ++i)
        {
            const double t = start + i;
            bool paused = false;
            for (const TelemetrySession::Pause &p : original->pauses())
                {
                    paused = paused || (t > p.startTime && t < p.endTime);
                }
            if (!paused)
                {
                    v[i] = speed->valueAt(t);
                }
        }
    const int half = original->derivedChannels().contains(ChannelId::Speed) ? kSmoothWorkedOut : kSmoothRecorded;
    v = smooth(v, half);

    const double mass = rider.riderKg + rider.bikeKg;
    const double rolling = crr(rider);
    const double dragArea = cda(rider);
    const double kept = 1.0 - std::clamp(rider.drivetrainLoss, 0.0, 50.0) / 100.0;

    QVector<double> watts(count, std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < count; ++i)
        {
            if (!std::isfinite(v.at(i)))
                {
                    continue;
                }
            const double t = start + i;
            const int before = std::max(0, i - half);
            const int after = std::min(count - 1, i + half);
            const double gain = after > before && std::isfinite(v.at(before)) && std::isfinite(v.at(after))
                                ? (v.at(after) - v.at(before)) / (after - before) : 0.0;
            bool ok = false;
            const double percent = grade ? grade->valueAt(t, &ok) : 0.0;
            const double angle = std::atan((ok ? percent : 0.0) / 100.0);
            const double height = elevation && !elevation->isEmpty() ? elevation->valueAt(t) : 0.0;
            const double celsius = temperature && !temperature->isEmpty() ? temperature->valueAt(t) : 15.0;
            double density = kSeaLevelDensity * std::exp(-height / kScaleHeight)
                             * kStandardKelvin / (273.15 + std::clamp(celsius, -40.0, 60.0));
            // the air the rider meets: the riding speed and what of the wind blows against it
            double airSpeed = v.at(i);
            if (windKnown)
                {
                    const Weather::Air air = weather.at(distance->valueAt(t), startUtc + qint64(t));
                    density = air.density();
                    airSpeed += reaching * air.windSpeed * std::cos((heading->valueAt(t) - air.windFrom) * kPi / 180.0);
                }

            // a wind from behind faster than the ride pushes
            const double force = 0.5 * density * dragArea * airSpeed * std::fabs(airSpeed)
                                 + mass * kGravity * (rolling * std::cos(angle) + std::sin(angle))
                                 + mass * gain;
            watts[i] = force * v.at(i) / kept;
        }
    // smoothed before anything is cut off: cutting first would keep the swings upwards only
    watts = smooth(watts, half);
    for (int i = 0; i < count; ++i)
        {
            // rolling downhill and braking take nothing from the legs; neither do still cranks
            const bool coasting = cadence && !cadence->isEmpty() && cadence->valueAt(start + i) < 1.0;
            if (std::isfinite(watts.at(i)))
                {
                    watts[i] = coasting ? 0.0 : std::max(0.0, watts.at(i));
                }
        }

    auto session = std::make_shared<TelemetrySession>(*original);
    const char *id = recorded && !recorded->isEmpty() ? ChannelId::PowerEstimated : ChannelId::Power;
    session->removeChannel(ChannelId::PowerEstimated);
    Channel &power = session->channelForWriting(id, "W");
    for (int i = 0; i < count; ++i)
        {
            if (std::isfinite(watts.at(i)))
                {
                    power.append(start + i, watts.at(i));
                }
        }
    if (power.isEmpty())
        {
            return original;
        }
    session->addDerivedChannel(id);
    return session;
}
