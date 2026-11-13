#include "units.h"

#include "../telemetry/channel.h"
#include "../telemetry/channelids.h"

#include <QRegularExpression>

#include <iterator>

namespace
{
// "relativeElevation" -> "Relative elevation", "engineRpm" -> "Engine rpm".
// Measurements other programs name (.vet templates) and this one has no
// data for yet are at least readable. Ids with other characters
// ("fit.record.87", "dev.Form Power") are left as they are.
QString readable(const QString &id)
{
    static const QRegularExpression camelCase("^[a-z][A-Za-z0-9]*$");
    if (!camelCase.match(id).hasMatch())
        {
            return id;
        }
    QString words;
    for (int i = 0; i < id.size(); ++i)
        {
            const QChar c = id.at(i);
            const bool startsWord = i > 0 && c.isUpper() && !id.at(i - 1).isUpper();
            words += startsWord ? QString(' ') + c.toLower() : QString(c);
        }
    words[0] = words.at(0).toUpper();
    return words;
}

const double kMetresPerSecondToKmh = 3.6;
const double kMetresPerSecondToMph = 2.2369362920544;
const double kMetresToFeet = 3.280839895;
const double kMetresToMiles = 1.0 / 1609.344;
}

double Units::speedScale() const
{
    return speed == MilesPerHour ? kMetresPerSecondToMph : kMetresPerSecondToKmh;
}

QString Units::speedLabel() const
{
    return speed == MilesPerHour ? ChannelFormat::tr("mph") : ChannelFormat::tr("km/h");
}

ChannelDisplay ChannelFormat::displayFor(const Channel &channel, const Units &units)
{
    return displayFor(channel.id(), channel.unit(), units);
}

ChannelDisplay ChannelFormat::displayFor(const QString &id, const QString &storedUnit, const Units &units)
{
    const bool imperial = units.length == Units::Imperial;
    if (id == ChannelId::PositionLat)
        {
            return {tr("Latitude"), "°", 1.0, 0.0, 5};
        }
    if (id == ChannelId::PositionLon)
        {
            return {tr("Longitude"), "°", 1.0, 0.0, 5};
        }
    if (id == ChannelId::Elevation)
        {
            return imperial ? ChannelDisplay{tr("Elevation"), tr("ft"), kMetresToFeet, 0.0, 0}
                   : ChannelDisplay{tr("Elevation"), tr("m"), 1.0, 0.0, 1};
        }
    if (id == ChannelId::Speed)
        {
            return {tr("Speed"), units.speedLabel(), units.speedScale(), 0.0, 1};
        }
    if (id == ChannelId::Distance)
        {
            return imperial ? ChannelDisplay{tr("Distance"), tr("mi"), kMetresToMiles, 0.0, 2}
                   : ChannelDisplay{tr("Distance"), tr("km"), 0.001, 0.0, 2};
        }
    if (id == ChannelId::Heading)
        {
            return {tr("Heading"), "°", 1.0, 0.0, 0};
        }
    if (id == ChannelId::Grade)
        {
            return {tr("Grade"), "%", 1.0, 0.0, 1};
        }
    if (id == ChannelId::HeartRate)
        {
            return {tr("Heart rate"), tr("bpm"), 1.0, 0.0, 0};
        }
    if (id == ChannelId::Cadence)
        {
            return {tr("Cadence"), tr("rpm"), 1.0, 0.0, 0};
        }
    if (id == ChannelId::Power)
        {
            return {tr("Power"), tr("W"), 1.0, 0.0, 0};
        }
    if (id == ChannelId::Temperature)
        {
            return units.temperature == Units::Fahrenheit
                   ? ChannelDisplay{tr("Temperature"), "°F", 9.0 / 5.0, 32.0, 0}
                   : ChannelDisplay{tr("Temperature"), "°C", 1.0, 0.0, 0};
        }
    if (id == ChannelId::GForceLateral)
        {
            return {tr("Lateral G"), "g", 1.0, 0.0, 2};
        }
    if (id == ChannelId::GForceLongitudinal)
        {
            return {tr("Longitudinal G"), "g", 1.0, 0.0, 2};
        }
    if (id == ChannelId::GForceVertical)
        {
            return {tr("Vertical G"), "g", 1.0, 0.0, 2};
        }
    if (id == ChannelId::Lap)
        {
            return {tr("Lap"), QString(), 1.0, 0.0, 0};
        }
    if (id == ChannelId::LapCount)
        {
            return {tr("Laps"), QString(), 1.0, 0.0, 0};
        }
    if (id == ChannelId::LapTime || id == ChannelId::PreviousLapTime || id == ChannelId::BestLapTime)
        {
            const QString name = id == ChannelId::LapTime ? tr("Lap time")
                                 : id == ChannelId::PreviousLapTime ? tr("Previous lap") : tr("Best lap");
            return {name, tr("s"), 1.0, 0.0, 2};
        }
    if (id == ChannelId::LapDelta)
        {
            return {tr("Delta to best lap"), tr("s"), 1.0, 0.0, 2};
        }
    if (id == ChannelId::Sector)
        {
            return {tr("Sector"), QString(), 1.0, 0.0, 0};
        }
    if (id == ChannelId::SectorTime || id == ChannelId::PreviousSectorTime)
        {
            return {id == ChannelId::SectorTime ? tr("Sector time") : tr("Previous sector"), tr("s"), 1.0, 0.0, 2};
        }
    if (id == ChannelId::SectorDelta)
        {
            return {tr("Sector against best"), tr("s"), 1.0, 0.0, 2};
        }
    if (id == ChannelId::GradeAngle)
        {
            return {tr("Grade angle"), "°", 1.0, 0.0, 1};
        }
    if (id == ChannelId::VerticalSpeed)
        {
            return imperial ? ChannelDisplay{tr("Vertical speed"), tr("ft/min"), kMetresToFeet * 60.0, 0.0, 0}
                   : ChannelDisplay{tr("Vertical speed"), tr("m/s"), 1.0, 0.0, 1};
        }
    if (id == ChannelId::TotalAscent || id == ChannelId::TotalDescent)
        {
            const QString name = id == ChannelId::TotalAscent ? tr("Total ascent") : tr("Total descent");
            return imperial ? ChannelDisplay{name, tr("ft"), kMetresToFeet, 0.0, 0} : ChannelDisplay{name, tr("m"), 1.0, 0.0, 0};
        }
    if (id == QLatin1String("gForceLatLon") || id == QLatin1String("gForceLatVert"))
        {
            return {tr("G-force"), "g", 1.0, 0.0, 2};
        }
    return {readable(id), storedUnit, 1.0, 0.0, 2};
}

ChannelDisplay ChannelFormat::displayIn(const QString &id, const QString &storedUnit, const QString &unit,
                                        const Units &units)
{
    ChannelDisplay display = displayFor(id, storedUnit, units);
    struct Conversion
    {
        const char *unit;
        double scale;
        double offset;
    };
    // from what the channels store (channelids.h)
    static const Conversion speeds[] = {{"mph", kMetresPerSecondToMph, 0.0}, {"km/h", kMetresPerSecondToKmh, 0.0},
        {"m/s", 1.0, 0.0}, {"kn", 3600.0 / 1852.0, 0.0}, {"ft/min", 60.0 / 0.3048, 0.0}
    };
    static const Conversion lengths[] = {{"ft", kMetresToFeet, 0.0}, {"m", 1.0, 0.0}, {"mi", kMetresToMiles, 0.0},
        {"km", 0.001, 0.0}
    };
    static const Conversion temperatures[] = {{"°F", 9.0 / 5.0, 32.0}, {"°C", 1.0, 0.0}};
    auto apply = [&](const Conversion *table, int count)
    {
        for (int i = 0; i < count; ++i)
            {
                if (unit == QString::fromUtf8(table[i].unit))
                    {
                        display.scale = table[i].scale;
                        display.offset = table[i].offset;
                        display.unit = unit;
                        return;
                    }
            }
    };
    // what is measured: known from the channel, or from the unit it stores
    if (id == ChannelId::Speed || storedUnit == QLatin1String("m/s"))
        {
            apply(speeds, int(std::size(speeds)));
        }
    else if (id == ChannelId::Elevation || id == ChannelId::Distance || storedUnit == QLatin1String("m"))
        {
            apply(lengths, int(std::size(lengths)));
        }
    else if (id == ChannelId::Temperature || storedUnit == QLatin1String("C") || storedUnit == QString::fromUtf8("°C"))
        {
            apply(temperatures, int(std::size(temperatures)));
        }
    return display;
}
