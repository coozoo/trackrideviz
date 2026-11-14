#include "datatypes.h"

#include "channelids.h"

#include <QHash>
#include <QRegularExpression>

namespace
{
struct TypeMapping
{
    const char *type;
    const char *channel;
};

// The measurements we have, or will have, a channel for. Anything else gets
// an id made from its name.
const TypeMapping kTypes[] =
{
    {"Speed", ChannelId::Speed},
    {"Altitude / Z Position", ChannelId::Elevation},
    {"Ground-Based Altitude", ChannelId::Elevation},
    {"Distance", ChannelId::Distance},
    {"Distance from Beginning", ChannelId::Distance},
    {"Heading", ChannelId::Heading},
    {"Grade", ChannelId::Grade},
    {"Latitude / Y Position", ChannelId::PositionLat},
    {"Longitude / X Position", ChannelId::PositionLon},
    {"Heart Rate", ChannelId::HeartRate},
    {"Cadence", ChannelId::Cadence},
    {"Power", ChannelId::Power},
    {"Air Temperature", ChannelId::Temperature},
    {"Lateral Gs", ChannelId::GForceLateral},
    {"Acceleration Gs", ChannelId::GForceLongitudinal},
    {"Vertical Gs", ChannelId::GForceVertical},
    {"Engine RPM", "engineRpm"},
    {"Current Lap Time", "lapTime"},
    {"Current Lap #", "lap"},
    {"Previous Lap Time", "previousLapTime"},
    {"Fastest Lap So Far, Time", "bestLapTime"},
    {"Fastest Lap So Far, Same Pos, Time Diff", "lapDelta"},
    {"Total Lap Count", "lapCount"},
    {"Data Running Time, Seconds", "elapsedTime"},
};
}

QString DataTypes::channelFor(const QString &dataType, QString *unit)
{
    // "Speed (MPH)": the unit is in brackets at the end
    static const QRegularExpression withUnit("^(.*?)\\s*\\(([^()]*)\\)\\s*$");
    QString name = dataType.trimmed();
    QString found;
    const QRegularExpressionMatch match = withUnit.match(name);
    if (match.hasMatch())
        {
            static const QHash<QString, QString> units
            {
                {"mph", "mph"}, {"kph", "km/h"}, {"m/s", "m/s"}, {"knots", "kn"}, {"fpm", "ft/min"}, {"feet", "ft"},
                {"meters", "m"}, {"miles", "mi"}, {"kilometers", "km"}, {"f", "°F"}, {"c", "°C"}, {"degrees", QString()},
            };
            const QString bracket = match.captured(2).trimmed().toLower();
            if (units.contains(bracket))
                {
                    name = match.captured(1);
                    found = units.value(bracket);
                }
        }
    if (unit)
        {
            *unit = found;
        }
    for (const TypeMapping &mapping : kTypes)
        {
            if (name.compare(QLatin1String(mapping.type), Qt::CaseInsensitive) == 0)
                {
                    return QString::fromLatin1(mapping.channel);
                }
        }
    // "Engine Oil Temperature" -> "engineOilTemperature"
    static const QRegularExpression separators("[^A-Za-z0-9]+");
    const QStringList words = name.split(separators, Qt::SkipEmptyParts);
    QString id;
    for (const QString &word : words)
        {
            id += id.isEmpty() ? word.toLower() : word.left(1).toUpper() + word.mid(1).toLower();
        }
    return id;
}

bool DataTypes::toStored(const QString &unit, double *scale, double *offset, QString *storedUnit)
{
    struct Conversion
    {
        double scale;
        double offset;
        const char *stored;
    };
    static const QHash<QString, Conversion> conversions
    {
        {"mph", {0.44704, 0.0, "m/s"}},
        {"km/h", {1.0 / 3.6, 0.0, "m/s"}},
        {"m/s", {1.0, 0.0, "m/s"}},
        {"kn", {1852.0 / 3600.0, 0.0, "m/s"}},
        {"ft/min", {0.3048 / 60.0, 0.0, "m/s"}},
        {"ft", {0.3048, 0.0, "m"}},
        {"m", {1.0, 0.0, "m"}},
        {"mi", {1609.344, 0.0, "m"}},
        {"km", {1000.0, 0.0, "m"}},
        {QStringLiteral("°F"), {5.0 / 9.0, -32.0 * 5.0 / 9.0, "C"}},
        {QStringLiteral("°C"), {1.0, 0.0, "C"}},
    };
    const auto found = conversions.constFind(unit);
    if (found == conversions.constEnd())
        {
            *scale = 1.0;
            *offset = 0.0;
            *storedUnit = unit;
            return false;
        }
    *scale = found->scale;
    *offset = found->offset;
    *storedUnit = QString::fromLatin1(found->stored);
    return true;
}

QString DataTypes::storedUnitOf(const QString &id)
{
    static const QHash<QString, QString> units
    {
        {ChannelId::PositionLat, "deg"}, {ChannelId::PositionLon, "deg"}, {ChannelId::Heading, "deg"},
        {ChannelId::Elevation, "m"}, {ChannelId::Distance, "m"}, {ChannelId::Speed, "m/s"},
        {ChannelId::Grade, "%"}, {ChannelId::HeartRate, "bpm"}, {ChannelId::Cadence, "rpm"},
        {ChannelId::Power, "W"}, {ChannelId::Temperature, "C"}, {ChannelId::GForceLateral, "g"},
        {ChannelId::GForceLongitudinal, "g"}, {ChannelId::GForceVertical, "g"},
        {ChannelId::PowerBalanceLeft, "%"}, {ChannelId::PowerBalanceRight, "%"}, {ChannelId::LeftPower, "W"}, {ChannelId::RightPower, "W"},
        {ChannelId::LeftTorqueEffectiveness, "%"}, {ChannelId::RightTorqueEffectiveness, "%"},
        {ChannelId::LeftPedalSmoothness, "%"}, {ChannelId::RightPedalSmoothness, "%"},
        {ChannelId::CombinedPedalSmoothness, "%"},
        {ChannelId::LeftPlatformCenterOffset, "mm"}, {ChannelId::RightPlatformCenterOffset, "mm"},
        {ChannelId::LeftPowerPhaseStart, "deg"}, {ChannelId::LeftPowerPhaseLength, "deg"},
        {ChannelId::LeftPowerPhasePeakStart, "deg"}, {ChannelId::LeftPowerPhasePeakLength, "deg"},
        {ChannelId::RightPowerPhaseStart, "deg"}, {ChannelId::RightPowerPhaseLength, "deg"},
        {ChannelId::RightPowerPhasePeakStart, "deg"}, {ChannelId::RightPowerPhasePeakLength, "deg"},
    };
    return units.value(id);
}
