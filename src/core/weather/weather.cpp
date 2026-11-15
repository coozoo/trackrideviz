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

// A service, a place and its days as one word: "open-meteo_49.23_28.45_2025-07-15_2025-07-15".
QString keyOf(const QString &provider, double latitude, double longitude, const QDate &first, const QDate &last)
{
    return QString("%1_%2_%3_%4_%5").arg(provider).arg(latitude, 0, 'f', 2).arg(longitude, 0, 'f', 2)
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
    air.seaLevel = a.seaLevel;
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

double Weather::Air::density(double elevation) const
{
    // a pressure at the sea is less higher up: by e every 8.4 km
    const double pressure = seaLevel ? pressureHpa * std::exp(-elevation / 8434.5) : pressureHpa;
    // dry air: the pressure over the gas constant and the temperature
    return pressure * 100.0 / (287.05 * (celsius + 273.15));
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

TelemetrySessionPtr WeatherChannels::add(const TelemetrySessionPtr &original, const Weather &weather,
        double windSharePercent)
{
    if (!original || weather.isEmpty() || !original->startUtc().isValid())
        {
            return original;
        }
    const Channel *distance = original->channel(ChannelId::Distance);
    const Channel *elevation = original->channel(ChannelId::Elevation);
    const Channel *heading = original->channel(ChannelId::Heading);
    if (!distance || distance->count() < 2)
        {
            return original;
        }
    auto session = std::make_shared<TelemetrySession>(*original);
    const auto fresh = [&](const char *id, const char *unit) -> Channel &
    {
        session->removeChannel(id);
        session->addDerivedChannel(id);
        return session->channelForWriting(id, unit);
    };
    Channel &speed = fresh(ChannelId::WindSpeed, "m/s");
    Channel &from = fresh(ChannelId::WindDirection, "deg");
    Channel &density = fresh(ChannelId::AirDensity, "kg/m3");
    Channel *against = heading && !heading->isEmpty() ? &fresh(ChannelId::Headwind, "m/s") : nullptr;
    Channel *felt = against ? &fresh(ChannelId::WindRelative, "deg") : nullptr;
    const qint64 startUtc = original->startUtc().toSecsSinceEpoch();
    const double reaching = std::clamp(windSharePercent, 0.0, 100.0) / 100.0;
    // the wind changes by the hour, but the rider turns by the second
    for (double t = distance->startTime(); t <= distance->endTime(); t += 1.0)
        {
            const Weather::Air air = weather.at(distance->valueAt(t), startUtc + qint64(t));
            speed.append(t, air.windSpeed);
            from.append(t, air.windFrom);
            density.append(t, air.density(elevation && !elevation->isEmpty() ? elevation->valueAt(t) : 0.0));
            if (against)
                {
                    const double turned = std::fmod(air.windFrom - heading->valueAt(t) + 720.0, 360.0);
                    against->append(t, reaching * air.windSpeed * std::cos(turned * kPi / 180.0));
                    felt->append(t, turned);
                }
        }
    return session;
}

TelemetrySessionPtr WeatherChannels::addWind(const TelemetrySessionPtr &original, const HandWind &wind,
        double windSharePercent)
{
    const Channel *heading = original ? original->channel(ChannelId::Heading) : nullptr;
    if (!wind.used || !heading || heading->count() < 2)
        {
            return original;
        }
    auto session = std::make_shared<TelemetrySession>(*original);
    const auto fresh = [&](const char *id, const char *unit) -> Channel &
    {
        session->removeChannel(id);
        session->addDerivedChannel(id);
        return session->channelForWriting(id, unit);
    };
    Channel &speed = fresh(ChannelId::WindSpeed, "m/s");
    Channel &from = fresh(ChannelId::WindDirection, "deg");
    Channel &against = fresh(ChannelId::Headwind, "m/s");
    Channel &felt = fresh(ChannelId::WindRelative, "deg");
    const double reaching = std::clamp(windSharePercent, 0.0, 100.0) / 100.0;
    for (double t = heading->startTime(); t <= heading->endTime(); t += 1.0)
        {
            const double turned = std::fmod(wind.from - heading->valueAt(t) + 720.0, 360.0);
            speed.append(t, wind.speed);
            from.append(t, wind.from);
            against.append(t, reaching * wind.speed * std::cos(turned * kPi / 180.0));
            felt.append(t, turned);
        }
    return session;
}

qint64 WeatherStore::storedBytes() const
{
    QString folder;
    {
        const QMutexLocker lock(&m_mutex);
        folder = m_folder;
    }
    qint64 bytes = 0;
    for (const QFileInfo &file : QDir(folder).entryInfoList({"*.json"}, QDir::Files))
        {
            bytes += file.size();
        }
    return bytes;
}

void WeatherStore::clearStored()
{
    {
        const QMutexLocker lock(&m_mutex);
        // only what is ours: the answers this store wrote
        for (const QFileInfo &file : QDir(m_folder).entryInfoList({"*.json"}, QDir::Files))
            {
                QFile::remove(file.absoluteFilePath());
            }
        m_failed.clear();
    }
    emit arrived();
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

QVector<WeatherStore::Provider> WeatherStore::providers()
{
    return
    {
        {"open-meteo", "Open-Meteo", false, "Weather data by Open-Meteo.com"},
        {"visualcrossing", "Visual Crossing", true, "Weather data by Visual Crossing"},
        {"weatherapi", "WeatherAPI.com", true, "Powered by WeatherAPI.com"},
    };
}

WeatherStore::Provider WeatherStore::provider(const QString &id)
{
    const QVector<Provider> all = providers();
    for (const Provider &p : all)
        {
            if (p.id == id)
                {
                    return p;
                }
        }
    return all.first();
}

QString WeatherStore::attribution(const QString &id)
{
    return provider(id).attribution;
}

QString WeatherStore::problem() const
{
    const QMutexLocker lock(&m_mutex);
    return m_problem;
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

QVector<Weather::Air> WeatherStore::parse(const QByteArray &json, qint64 firstHour)
{
    // every hour the answer has, by its time; values it has nothing for are as the hour before's
    QVector<QPair<qint64, Weather::Air>> readings;
    Weather::Air air;
    const auto keep = [&](qint64 when, const QJsonValue &speed, double speedScale, const QJsonValue &from,
                          const QJsonValue &celsius, const QJsonValue &pressure)
    {
        if (speed.isDouble() && from.isDouble())
            {
                air.windSpeed = speed.toDouble() * speedScale;
                air.windFrom = from.toDouble();
            }
        if (celsius.isDouble())
            {
                air.celsius = celsius.toDouble();
            }
        if (pressure.isDouble())
            {
                air.pressureHpa = pressure.toDouble();
            }
        readings.append({when, air});
    };
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.contains("hourly"))
        {
            // Open-Meteo: lists side by side, the times in UTC without a zone
            const QJsonObject hourly = root.value("hourly").toObject();
            const QJsonArray times = hourly.value("time").toArray();
            const QJsonArray speed = hourly.value("wind_speed_10m").toArray();
            const QJsonArray from = hourly.value("wind_direction_10m").toArray();
            const QJsonArray celsius = hourly.value("temperature_2m").toArray();
            const QJsonArray pressure = hourly.value("surface_pressure").toArray();
            if (from.size() != speed.size() || celsius.size() != speed.size() || pressure.size() != speed.size())
                {
                    return {};
                }
            for (int i = 0; i < speed.size(); ++i)
                {
                    QDateTime when = QDateTime::fromString(times.at(i).toString(), "yyyy-MM-ddTHH:mm");
                    when.setTimeZone(QTimeZone::UTC);
                    // without times (older answers kept on disk): an hour each from the first
                    const qint64 at = when.isValid() ? when.toSecsSinceEpoch() : firstHour + 3600 * qint64(i);
                    keep(at, speed.at(i), 1.0, from.at(i), celsius.at(i), pressure.at(i));
                }
        }
    else if (root.contains("days"))
        {
            // Visual Crossing: days of hours, km/h, the pressure at the sea
            air.seaLevel = true;
            for (const QJsonValue &day : root.value("days").toArray())
                {
                    for (const QJsonValue &value : day.toObject().value("hours").toArray())
                        {
                            const QJsonObject hour = value.toObject();
                            keep(qint64(hour.value("datetimeEpoch").toDouble()), hour.value("windspeed"), 1.0 / 3.6,
                                 hour.value("winddir"), hour.value("temp"), hour.value("pressure"));
                        }
                }
        }
    else if (root.contains("forecast"))
        {
            // WeatherAPI.com: the same, other names
            air.seaLevel = true;
            for (const QJsonValue &day : root.value("forecast").toObject().value("forecastday").toArray())
                {
                    for (const QJsonValue &value : day.toObject().value("hour").toArray())
                        {
                            const QJsonObject hour = value.toObject();
                            keep(qint64(hour.value("time_epoch").toDouble()), hour.value("wind_kph"), 1.0 / 3.6,
                                 hour.value("wind_degree"), hour.value("temp_c"), hour.value("pressure_mb"));
                        }
                }
        }
    if (readings.isEmpty())
        {
            return {};
        }
    std::sort(readings.begin(), readings.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    // hour by hour from the first wanted: each the last reading by then (the first before any)
    QVector<Weather::Air> hours;
    const qint64 last = std::max(readings.last().first, firstHour);
    int reading = 0;
    for (qint64 at = firstHour; at <= last; at += 3600)
        {
            while (reading + 1 < readings.size() && readings.at(reading + 1).first <= at)
                {
                    ++reading;
                }
            hours.append(readings.at(reading).second);
        }
    return hours;
}

Weather WeatherStore::along(const TelemetrySession &session, const WeatherChoice &choice, bool *waiting)
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
    const double spacing = std::max(1.0, choice.spacingKm) * 1000.0;
    const Provider service = provider(choice.provider);
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
    m_apiKeys.insert(service.id, choice.apiKey.trimmed());
    // nothing to ask without the key the service wants
    const bool askable = !service.needsKey || !choice.apiKey.trimmed().isEmpty();
    if (!askable)
        {
            m_problem = tr("%1 wants a key of its own: enter it on the Weather page of the settings.").arg(service.name);
        }
    for (const double mark : std::as_const(marks))
        {
            while (sample < distance->count() - 1 && distance->valueAtIndex(sample) < mark)
                {
                    ++sample;
                }
            const double t = distance->timeAt(sample);
            const QString key = keyOf(service.id, lat->valueAt(t), lon->valueAt(t), first, last);
            if (!read.contains(key))
                {
                    QFile file(pathOf(key));
                    read.insert(key, file.open(QIODevice::ReadOnly) ? parse(file.readAll(), weather.firstHour) : QVector<Weather::Air>());
                }
            if (!read.value(key).isEmpty())
                {
                    weather.places.append({mark, read.value(key)});
                }
            else if (askable && !m_failed.contains(key))
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
            // service, latitude, longitude, first and last day
            const QStringList parts = key.split('_');
            const QString service = parts.value(0);
            QString apiKey;
            {
                const QMutexLocker lock(&m_mutex);
                apiKey = m_apiKeys.value(service);
            }
            QString address;
            if (service == QLatin1String("visualcrossing"))
                {
                    address = QString("https://weather.visualcrossing.com/VisualCrossingWebServices/rest/services/timeline/"
                                      "%1,%2/%3/%4?unitGroup=metric&include=hours&contentType=json"
                                      "&elements=datetimeEpoch,temp,windspeed,winddir,pressure&key=%5")
                              .arg(parts.value(1), parts.value(2), parts.value(3), parts.value(4),
                                   QString::fromLatin1(QUrl::toPercentEncoding(apiKey)));
                }
            else if (service == QLatin1String("weatherapi"))
                {
                    address = QString("https://api.weatherapi.com/v1/history.json?key=%1&q=%2,%3&dt=%4")
                              .arg(QString::fromLatin1(QUrl::toPercentEncoding(apiKey)), parts.value(1), parts.value(2),
                                   parts.value(3));
                    if (parts.value(4) != parts.value(3))
                        {
                            address += "&end_dt=" + parts.value(4);
                        }
                }
            else
                {
                    const QDate first = QDate::fromString(parts.value(3), Qt::ISODate);
                    const bool recent = first.daysTo(QDateTime::currentDateTimeUtc().date()) < kHistoryLagDays;
                    address = QString("%1?latitude=%2&longitude=%3&start_date=%4&end_date=%5"
                                      "&hourly=temperature_2m,surface_pressure,wind_speed_10m,wind_direction_10m"
                                      "&wind_speed_unit=ms")
                              .arg(recent ? kRecent : kHistory, parts.value(1), parts.value(2), parts.value(3), parts.value(4));
                }
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
                const bool answered = reply->error() == QNetworkReply::NoError && !parse(data, 0).isEmpty();
                if (answered)
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
                            // what the service says, as far as it says something short
                            const QString said = QString::fromUtf8(data.left(200)).simplified();
                            m_problem = answered ? tr("The weather could not be kept on disk.")
                                        : tr("The weather service refused: %1").arg(said.isEmpty() ? reply->errorString() : said);
                        }
                    else
                        {
                            m_problem.clear();
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
