#include "units.h"

#include "../telemetry/channel.h"
#include "../telemetry/channelids.h"

namespace
{
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
    return {id, storedUnit, 1.0, 0.0, 2};
}
