#pragma once

#include "../telemetry/telemetrysession.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QVector>

class QNetworkAccessManager;

// The wind and the air along a ride on the day it was ridden, hour by hour
// at places some kilometers apart, as a weather history service has them.
struct Weather
{
    struct Air
    {
        double windSpeed{0.0};        // m/s, 10 m above the ground
        double windFrom{0.0};         // degrees, where it blows from: 0 = north, clockwise
        double celsius{15.0};
        double pressureHpa{1013.25};  // at the ground there ...
        bool seaLevel{false};         // ... or, as some services give it, brought down to the sea

        // kg/m3 at `elevation` meters (used only for a pressure at the sea)
        double density(double elevation = 0.0) const;
    };

    struct Place
    {
        double distance{0.0};         // m along the ride
        QVector<Air> hours;           // from `firstHour` on
    };

    qint64 firstHour{0};              // seconds since 1970 UTC
    QVector<Place> places;

    bool isEmpty() const { return places.isEmpty(); }
    // Between the places by the distance ridden, between the hours by the clock.
    Air at(double distance, qint64 secondsUtc) const;
};

// Whether the weather of rides is asked for, and how.
struct WeatherChoice
{
    bool enabled{false};
    QString provider{"open-meteo"};   // one of WeatherStore::providers()
    QString apiKey;                   // for a service that wants one
    double spacingKm{10.0};   // a place this often along the ride
    // % of the wind 10 m up that reaches a rider: less between houses and trees.
    double windShare{60.0};

    bool operator==(const WeatherChoice &other) const
    {
        return enabled == other.enabled && provider == other.provider && apiKey == other.apiKey
               && spacingKm == other.spacingKm && windShare == other.windShare;
    }
    bool operator!=(const WeatherChoice &other) const { return !(*this == other); }
};

// A wind the user knows of: for a ride the weather history has nothing for, or no connection.
struct HandWind
{
    bool used{false};
    double speed{0.0};   // m/s, 10 m above the ground as forecasts give it
    double from{0.0};    // degrees, where it blows from: 0 = north, clockwise

    bool operator==(const HandWind &other) const
    {
        return used == other.used && speed == other.speed && from == other.from;
    }
    bool operator!=(const HandWind &other) const { return !(*this == other); }
};

// The weather as measurements of the ride (channelids.h): the wind's speed and direction, how
// much of it blows against the rider and from where the rider feels it, and the air's density.
namespace WeatherChannels
{
// A copy of `session` with them; the session itself without weather or positions.
// Wants the distance and the heading in place (DerivedChannels::complete).
TelemetrySessionPtr add(const TelemetrySessionPtr &session, const Weather &weather, double windSharePercent);
// The same wind all along the ride: the wind's channels without the air's density.
TelemetrySessionPtr addWind(const TelemetrySessionPtr &session, const HandWind &wind, double windSharePercent);
}

// Fetches the weather of rides and keeps it on disk: a ride asked about once
// needs no connection again.
//
// along() may be called from any thread and only reads the disk. What is
// missing is asked for on the store's own thread, and arrived() says when
// asking again is worth it. Nothing is fetched until setDownloads(true).
class WeatherStore : public QObject
{
    Q_OBJECT

public:
    static WeatherStore *instance();

    // A weather history service.
    struct Provider
    {
        QString id;            // kept in the settings
        QString name;
        bool needsKey{false};
        QString attribution;
    };
    // The services the program knows; the first is the one it starts with.
    static QVector<Provider> providers();
    // By id; the first one when the id is not known.
    static Provider provider(const QString &id);

    // The weather at places `choice.spacingKm` apart along the ride, from the
    // service chosen. Empty when the recording has no positions or no date, or
    // nothing of it is on disk; *waiting says that some of it has been asked for.
    Weather along(const TelemetrySession &session, const WeatherChoice &choice, bool *waiting = nullptr);
    // Why the last answer was refused (a wrong key, no connection); empty when nothing was.
    QString problem() const;

    void setDownloads(bool allowed);
    void setFolder(const QString &folder);
    // Places asked for and not there yet, while downloading is allowed.
    int pending() const;
    // Bytes on disk; and all of them thrown away (arrived() follows: what is shown asks again).
    qint64 storedBytes() const;
    void clearStored();

    // Whose data it is, to be said where it is used.
    static QString attribution(const QString &provider);

    // What one answer of a service holds (any of providers(): told apart by its shape), hour by
    // hour from `firstHour` (seconds since 1970 UTC) to its last; empty when it is no such answer.
    static QVector<Weather::Air> parse(const QByteArray &json, qint64 firstHour);

signals:
    // What was asked for has come (or failed for good).
    void arrived();

private:
    WeatherStore();
    // Where the weather of one place and stretch of days is kept; also what it is known by.
    QString pathOf(const QString &key) const;
    Q_INVOKABLE void fetchWanted();
    void startNext();

    mutable QMutex m_mutex;
    QString m_folder;
    bool m_downloads{false};
    QSet<QString> m_wanted;
    QSet<QString> m_failed;   // not asked for again in this run
    QSet<QString> m_running;
    QHash<QString, QString> m_apiKeys;   // by provider
    QString m_problem;
    QNetworkAccessManager *m_network{nullptr};
};
