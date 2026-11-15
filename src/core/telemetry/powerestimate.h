#pragma once

#include "telemetrysession.h"
#include "../weather/weather.h"

#include <QString>
#include <QStringList>

// The power a ride took, for recordings without a power meter: what air
// drag, rolling resistance, the climb and the gain of speed ask for at each
// moment, from the speed, the grade and the elevation, and from what is known
// of the rider and the bike.
//
// It is an estimate. Without the weather of the day the wind is not known,
// so a headwind reads low and a tailwind high; with it (weather.h) the wind
// along the way and the air's own density are counted in. A recorded power is never replaced: the estimate then goes
// beside it as a measurement of its own (ChannelId::PowerEstimated), so the
// two can be compared and the rider's numbers set by a real power meter.
// Either is listed in TelemetrySession::derivedChannels().
namespace PowerEstimate
{
// Who rides what. Filled in once in the settings.
struct Rider
{
    bool enabled{false};
    double riderKg{75.0};
    double heightCm{178.0};
    double bikeKg{10.0};
    // One of tires(), or OwnValue for `crr`.
    QString tires{"road-typical"};
    double crr{0.005};
    // One of positions(), or OwnValue for `cda`.
    QString position{"hoods"};
    double cda{0.36};          // m2
    double drivetrainLoss{3.0}; // % of the power at the cranks
    // The wind and the air of the day from a weather history service, at places this far apart.
    bool weather{false};
    double weatherSpacingKm{10.0};
    // % of the wind 10 m up that reaches a rider: less between houses and trees.
    double windShare{60.0};

    bool operator==(const Rider &other) const
    {
        return enabled == other.enabled && riderKg == other.riderKg && heightCm == other.heightCm
               && bikeKg == other.bikeKg && tires == other.tires && crr == other.crr
               && position == other.position && cda == other.cda && drivetrainLoss == other.drivetrainLoss
               && weather == other.weather && weatherSpacingKm == other.weatherSpacingKm && windShare == other.windShare;
    }
    bool operator!=(const Rider &other) const { return !(*this == other); }
};

inline constexpr char OwnValue[] = "own";

// "road-fast", "road-typical", "road-slow", then "gravel-" and "mtb-" alike.
QStringList tires();
// "tops", "hoods", "drops", "aerobars", "upright".
QStringList positions();
// Rolling resistance of one of tires() on typical asphalt; 0 for an unknown id.
double crrOf(const QString &tires);
// Drag area in m2 of a rider of this build in one of positions(); 0 for an unknown id.
double cdaOf(const QString &position, double riderKg, double heightCm);
// What the estimate uses: the rider's own value or that of the chosen kind.
double crr(const Rider &rider);
double cda(const Rider &rider);

// "road", "gravel" or "mtb" for a tire size as printed on the tire ("700x25c",
// "26x1.95", "40-622"), by its width; empty when the size cannot be read.
QString tireKind(const QString &size);

// A copy of `session` with the power added; the session itself when the
// estimate is off or there is no speed to work from.
// Wants the speed and the grade in place (DerivedChannels::complete), and for
// the wind the heading and the distance too.
TelemetrySessionPtr apply(const TelemetrySessionPtr &session, const Rider &rider, const Weather &weather = {});
}
