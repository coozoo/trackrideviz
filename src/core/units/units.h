#pragma once

#include <QCoreApplication>
#include <QString>

class Channel;

// Display units. Channels always store SI (see channelids.h); this only
// decides what the user reads in tables and on gauges.
struct Units
{
    enum Speed { KilometresPerHour, MilesPerHour };
    enum Length { Metric, Imperial };          // elevation m / ft, distance km / mi
    enum Temperature { Celsius, Fahrenheit };

    Speed speed{KilometresPerHour};
    Length length{Metric};
    Temperature temperature{Celsius};

    bool operator==(const Units &other) const
    {
        return speed == other.speed && length == other.length && temperature == other.temperature;
    }
    bool operator!=(const Units &other) const { return !(*this == other); }

    // Multiplier from m/s, and the label that goes with it.
    double speedScale() const;
    QString speedLabel() const;
};

// How one channel is presented: shown = stored * scale + offset.
struct ChannelDisplay
{
    QString name;
    QString unit;
    double scale{1.0};
    double offset{0.0};
    int decimals{1};

    double convert(double stored) const { return stored * scale + offset; }
};

class ChannelFormat
{
    Q_DECLARE_TR_FUNCTIONS(ChannelFormat)

public:
    // Channels the application has no name for keep their id, unit and raw value.
    static ChannelDisplay displayFor(const QString &channelId, const QString &storedUnit, const Units &units);
    static ChannelDisplay displayFor(const Channel &channel, const Units &units);
    // The same in a unit of its own ("mph", "km/h", "m/s", "kn", "ft", "m",
    // "mi", "km", "°F", "°C"), for gauges printed in one. The ordinary display
    // when the channel does not measure what that unit does.
    static ChannelDisplay displayIn(const QString &channelId, const QString &storedUnit, const QString &unit,
                                    const Units &units);
};
