#include "units.h"

#include "../telemetry/channel.h"
#include "../telemetry/channelids.h"

#include <QRegularExpression>

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
    if (id == QLatin1String("gForceLatLon") || id == QLatin1String("gForceLatVert"))
        {
            return {tr("G-force"), "g", 1.0, 0.0, 2};
        }
    return {readable(id), storedUnit, 1.0, 0.0, 2};
}
