#include "weather.h"

#include "../telemetry/channelids.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointF>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace
{
const double kPi = 3.14159265358979323846;
const int kParallel = 4;
// the history lags some days behind; the days before it come from the service's forecasts as they were
const int kHistoryLagDays = 7;
const char kHistory[] = "https://archive-api.open-meteo.com/v1/archive";
const char kRecent[] = "https://api.open-meteo.com/v1/forecast";

// A place and its days as one word: "49.23_28.45_2025-07-15_2025-07-15".
QString keyOf(double latitude, double longitude, const QDate &first, const QDate &last)
{
    return QString("%1_%2_%3_%4").arg(latitude, 0, 'f', 2).arg(longitude, 0, 'f', 2)
           .arg(first.toString(Qt::ISODate), last.toString(Qt::ISODate));
}

Weather::Air blend(const Weather::Air &a, const Weather::Air &b, double share)
{
    // the wind as north and east parts: 350 and 10 degrees meet at north, not at south
    const auto parts = [](const Weather::Air & air)
    {
        const double angle = air.windFrom * kPi / 180.0;
        return QPointF(air.windSpeed * std::cos(angle), air.windSpeed * std::sin(angle));
    };
    const QPointF wind = parts(a) * (1.0 - share) + parts(b) * share;
    Weather::Air air;
    air.windSpeed = std::hypot(wind.x(), wind.y());
    air.windFrom = std::fmod(std::atan2(wind.y(), wind.x()) * 180.0 / kPi + 360.0, 360.0);
    air.celsius = a.celsius * (1.0 - share) + b.celsius * share;
    air.pressureHpa = a.pressureHpa * (1.0 - share) + b.pressureHpa * share;
    return air;
}

Weather::Air atHour(const QVector<Weather::Air> &hours, double hour)
{
    if (hours.isEmpty())
        {
            return {};
        }
    const double held = std::clamp(hour, 0.0, double(hours.size() - 1));
    const int before = int(held);
    const int after = std::min(before + 1, int(hours.size()) - 1);
    return blend(hours.at(before), hours.at(after), held - before);
}
}

double Weather::Air::density() const
{
    // dry air: the pressure over the gas constant and the temperature
    return pressureHpa * 100.0 / (287.05 * (celsius + 273.15));
}

Weather::Air Weather::at(double distance, qint64 secondsUtc) const
{
    if (places.isEmpty())
        {
            return {};
        }
    const double hour = double(secondsUtc - firstHour) / 3600.0;
    int after = 0;
    while (after < places.size() - 1 && places.at(after).distance < distance)
        {
            ++after;
        }
    const int before = std::max(0, after - 1);
    const double span = places.at(after).distance - places.at(before).distance;
    const double share = span > 0.0 ? std::clamp((distance - places.at(before).distance) / span, 0.0, 1.0) : 0.0;
    return blend(atHour(places.at(before).hours, hour), atHour(places.at(after).hours, hour), share);
}

WeatherStore *WeatherStore::instance()
{
    static WeatherStore *store = new WeatherStore;
    return store;
}

WeatherStore::WeatherStore()
    : m_folder(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/weather")
{
}

QString WeatherStore::attribution()
{
    return QStringLiteral("Weather data by Open-Meteo.com");
}

void WeatherStore::setDownloads(bool allowed)
{
    bool start = false;
    {
        const QMutexLocker lock(&m_mutex);
        start = allowed && !m_downloads && !m_wanted.isEmpty();
        m_downloads = allowed;
    }
    if (start)
        {
            QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
        }
}

void WeatherStore::setFolder(const QString &folder)
{
    const QMutexLocker lock(&m_mutex);
    m_folder = folder;
    m_wanted.clear();
    m_failed.clear();
}

int WeatherStore::pending() const
{
    const QMutexLocker lock(&m_mutex);
    return m_downloads ? int(m_wanted.size() + m_running.size()) : 0;
}

QString WeatherStore::pathOf(const QString &key) const
{
    return m_folder + "/" + key + ".json";
}

QVector<Weather::Air> WeatherStore::parse(const QByteArray &json)
{
    const QJsonObject hourly = QJsonDocument::fromJson(json).object().value("hourly").toObject();
    const QJsonArray speed = hourly.value("wind_speed_10m").toArray();
    const QJsonArray from = hourly.value("wind_direction_10m").toArray();
    const QJsonArray celsius = hourly.value("temperature_2m").toArray();
    const QJsonArray pressure = hourly.value("surface_pressure").toArray();
    QVector<Weather::Air> hours;
    if (speed.isEmpty() || from.size() != speed.size() || celsius.size() != speed.size() || pressure.size() != speed.size())
        {
            return hours;
        }
    for (int i = 0; i < speed.size(); ++i)
        {
            // hours the service has nothing for yet are null: as the hour before
            Weather::Air air = hours.isEmpty() ? Weather::Air() : hours.last();
            if (speed.at(i).isDouble() && from.at(i).isDouble())
                {
                    air.windSpeed = speed.at(i).toDouble();
                    air.windFrom = from.at(i).toDouble();
                }
            if (celsius.at(i).isDouble())
                {
                    air.celsius = celsius.at(i).toDouble();
                }
            if (pressure.at(i).isDouble())
                {
                    air.pressureHpa = pressure.at(i).toDouble();
                }
            hours.append(air);
        }
    return hours;
}

Weather WeatherStore::along(const TelemetrySession &session, double spacingKm, bool *waiting)
{
    if (waiting)
        {
            *waiting = false;
        }
    Weather weather;
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    const Channel *distance = session.channel(ChannelId::Distance);
    if (!lat || !lon || !distance || lat->isEmpty() || lon->isEmpty() || distance->count() < 2 || !session.startUtc().isValid())
        {
            return weather;
        }
    const QDateTime start = session.startUtc().toUTC();
    const QDate first = start.date();
    const QDate last = start.addSecs(qint64(distance->endTime())).date();
    weather.firstHour = QDateTime(first, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();

    // where the ride was after every `spacing`, and where it ended
    const double spacing = std::max(1.0, spacingKm) * 1000.0;
    const double begun = distance->valueAtIndex(0);
    const double whole = distance->valueAtIndex(distance->count() - 1);
    QVector<double> marks;
    for (double mark = begun; mark < whole; mark += spacing)
        {
            marks.append(mark);
        }
    if (marks.isEmpty() || whole - marks.last() > spacing / 2.0)
        {
            marks.append(whole);
        }

    bool asked = false;
    int sample = 0;
    QHash<QString, QVector<Weather::Air>> read;   // places close together are one to the service
    const QMutexLocker lock(&m_mutex);
    for (const double mark : std::as_const(marks))
        {
            while (sample < distance->count() - 1 && distance->valueAtIndex(sample) < mark)
                {
                    ++sample;
                }
            const double t = distance->timeAt(sample);
            const QString key = keyOf(lat->valueAt(t), lon->valueAt(t), first, last);
            if (!read.contains(key))
                {
                    QFile file(pathOf(key));
                    read.insert(key, file.open(QIODevice::ReadOnly) ? parse(file.readAll()) : QVector<Weather::Air>());
                }
            if (!read.value(key).isEmpty())
                {
                    weather.places.append({mark, read.value(key)});
                }
            else if (!m_failed.contains(key))
                {
                    asked = asked || (!m_wanted.contains(key) && !m_running.contains(key));
                    if (!m_running.contains(key))
                        {
                            m_wanted.insert(key);
                        }
                    if (waiting)
                        {
                            *waiting = m_downloads;
                        }
                }
        }
    if (asked && m_downloads)
        {
            QMetaObject::invokeMethod(this, "fetchWanted", Qt::QueuedConnection);
        }
    return weather;
}

void WeatherStore::fetchWanted()
{
    if (!m_network)
        {
            m_network = new QNetworkAccessManager(this);
        }
    startNext();
}

void WeatherStore::startNext()
{
    for (;;)
        {
            QString key;
            QString path;
            {
                const QMutexLocker lock(&m_mutex);
                if (!m_downloads || m_wanted.isEmpty() || m_running.size() >= kParallel)
                    {
                        return;
                    }
                key = *m_wanted.constBegin();
                m_wanted.remove(key);
                m_running.insert(key);
                path = pathOf(key);
            }
            const QStringList parts = key.split('_');
            const QDate first = QDate::fromString(parts.value(2), Qt::ISODate);
            const bool recent = first.daysTo(QDateTime::currentDateTimeUtc().date()) < kHistoryLagDays;
            const QString address = QString("%1?latitude=%2&longitude=%3&start_date=%4&end_date=%5"
                                            "&hourly=temperature_2m,surface_pressure,wind_speed_10m,wind_direction_10m"
                                            "&wind_speed_unit=ms")
                                    .arg(recent ? kRecent : kHistory, parts.value(0), parts.value(1), parts.value(2), parts.value(3));
            QNetworkRequest request{QUrl(address)};
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QString("%1/%2 (+https://github.com/coozoo/trackrideviz)")
                              .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));
            request.setTransferTimeout(20000);
            QNetworkReply *reply = m_network->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, key, path]()
            {
                reply->deleteLater();
                const QByteArray data = reply->readAll();
                bool kept = false;
                if (reply->error() == QNetworkReply::NoError && !parse(data).isEmpty())
                    {
                        QDir().mkpath(QFileInfo(path).path());
                        QSaveFile file(path);
                        kept = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
                    }
                bool done = false;
                {
                    const QMutexLocker lock(&m_mutex);
                    m_running.remove(key);
                    if (!kept)
                        {
                            m_failed.insert(key);
                        }
                    done = m_running.isEmpty() && m_wanted.isEmpty();
                }
                if (done)
                    {
                        emit arrived();
                    }
                startNext();
            });
        }
}
