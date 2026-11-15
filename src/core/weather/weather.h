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
        double pressureHpa{1013.25};  // at the ground there, not brought down to the sea

        // kg/m3
        double density() const;
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

    // The weather at places `spacingKm` apart along the ride. Empty when the
    // recording has no positions or no date, or nothing of it is on disk;
    // *waiting says that some of it has been asked for.
    Weather along(const TelemetrySession &session, double spacingKm, bool *waiting = nullptr);

    void setDownloads(bool allowed);
    void setFolder(const QString &folder);
    // Places asked for and not there yet, while downloading is allowed.
    int pending() const;

    // Whose data it is, to be said where it is used.
    static QString attribution();

    // What one answer of the service holds, from the first hour of its first day; empty when it
    // is not such an answer.
    static QVector<Weather::Air> parse(const QByteArray &json);

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
    QNetworkAccessManager *m_network{nullptr};
};
