#include "tcximporter.h"

#include "channelids.h"

#include <QCoreApplication>
#include <QFile>
#include <QXmlStreamReader>

#include <cmath>
#include <limits>

namespace
{
const double kNoValue = std::numeric_limits<double>::quiet_NaN();

struct Point
{
    qint64 utcMs{0};
    bool hasTime{false};
    double lat{kNoValue};
    double lon{kNoValue};
    double ele{kNoValue};
    double distance{kNoValue};
    double hr{kNoValue};
    double cad{kNoValue};
    double power{kNoValue};
    double speed{kNoValue};
};

struct FileLap
{
    qint64 startMs{0};
    double seconds{0.0};
};

double toDouble(const QString &text)
{
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    return ok ? v : kNoValue;
}

qint64 toMs(const QString &text, bool *ok)
{
    const QDateTime time = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
    *ok = time.isValid();
    return time.toMSecsSinceEpoch();
}

// Everything inside a <Trackpoint>, at any depth: the heart rate sits in a <Value>, speed and
// power in an extension whose name and namespace differ between devices.
Point readTrackPoint(QXmlStreamReader &xml)
{
    Point point;
    int depth = 1;
    bool inHeartRate = false;
    while (depth > 0 && !xml.atEnd())
        {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::EndElement)
                {
                    if (xml.name().toString().toLower() == QLatin1String("heartratebpm"))
                        {
                            inHeartRate = false;
                        }
                    --depth;
                    continue;
                }
            if (token != QXmlStreamReader::StartElement)
                {
                    continue;
                }
            const QString name = xml.name().toString().toLower();
            double *target = nullptr;
            if (name == "time")
                {
                    point.utcMs = toMs(xml.readElementText(QXmlStreamReader::SkipChildElements), &point.hasTime);
                    continue;
                }
            if (name == "latitudedegrees")
                {
                    target = &point.lat;
                }
            else if (name == "longitudedegrees")
                {
                    target = &point.lon;
                }
            else if (name == "altitudemeters")
                {
                    target = &point.ele;
                }
            else if (name == "distancemeters")
                {
                    target = &point.distance;
                }
            else if (name == "value" && inHeartRate)
                {
                    target = &point.hr;
                }
            else if (name == "cadence" || name == "runcadence")
                {
                    target = &point.cad;
                }
            else if (name == "watts")
                {
                    target = &point.power;
                }
            else if (name == "speed")
                {
                    target = &point.speed;
                }
            if (target)
                {
                    // consumes the matching end element as well
                    *target = toDouble(xml.readElementText(QXmlStreamReader::SkipChildElements));
                }
            else
                {
                    inHeartRate = inHeartRate || name == "heartratebpm";
                    ++depth;
                }
        }
    return point;
}

void appendIfValid(Channel &channel, double time, double value)
{
    if (!std::isnan(value))
        {
            channel.append(time, value);
        }
}
}

TelemetrySessionPtr TcxImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = QCoreApplication::translate("TcxImporter", "Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(&file, error);
}

TelemetrySessionPtr TcxImporter::read(QIODevice *device, QString *error)
{
    QXmlStreamReader xml(device);
    QVector<Point> points;
    QVector<FileLap> laps;
    QString sport;
    bool isTcx = false;
    while (!xml.atEnd())
        {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                {
                    continue;
                }
            const QString name = xml.name().toString().toLower();
            if (name == "trainingcenterdatabase")
                {
                    isTcx = true;
                }
            else if (name == "activity" && sport.isEmpty())
                {
                    sport = xml.attributes().value("Sport").toString();
                }
            else if (name == "lap")
                {
                    bool ok = false;
                    const qint64 start = toMs(xml.attributes().value("StartTime").toString(), &ok);
                    if (ok)
                        {
                            laps.append({start, 0.0});
                        }
                }
            else if (name == "totaltimeseconds" && !laps.isEmpty() && laps.last().seconds <= 0.0)
                {
                    const double seconds = toDouble(xml.readElementText());
                    laps.last().seconds = std::isnan(seconds) ? 0.0 : seconds;
                }
            else if (name == "trackpoint")
                {
                    const Point point = readTrackPoint(xml);
                    if (xml.hasError())
                        {
                            // the file ends inside this point; what we have of it is not reliable
                            break;
                        }
                    if (point.hasTime)
                        {
                            points.append(point);
                        }
                }
        }
    if ((xml.hasError() && points.isEmpty()) || !isTcx)
        {
            if (error)
                {
                    *error = xml.hasError()
                             ? QCoreApplication::translate("TcxImporter", "Not a valid TCX file: %1 (line %2)")
                             .arg(xml.errorString()).arg(xml.lineNumber())
                             : QCoreApplication::translate("TcxImporter", "Not a TCX file.");
                }
            return nullptr;
        }
    if (points.isEmpty())
        {
            if (error)
                {
                    *error = QCoreApplication::translate("TcxImporter", "The TCX file contains no track points.");
                }
            return nullptr;
        }

    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(sport.isEmpty() ? QString("TCX") : QString("TCX, %1").arg(sport));
    if (xml.hasError())
        {
            session->addWarning(QCoreApplication::translate("TcxImporter", "The file is damaged after line %1; only the points before it were read.")
                                .arg(xml.lineNumber()));
        }
    const qint64 startMs = points.first().utcMs;
    session->setStartUtc(QDateTime::fromMSecsSinceEpoch(startMs).toUTC());

    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &ele = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    Channel &distance = session->channelForWriting(ChannelId::Distance, "m");
    Channel &hr = session->channelForWriting(ChannelId::HeartRate, "bpm");
    Channel &cad = session->channelForWriting(ChannelId::Cadence, "rpm");
    Channel &power = session->channelForWriting(ChannelId::Power, "W");
    for (const Point &p : points)
        {
            // points out of order are dropped by the channels themselves
            const double t = double(p.utcMs - startMs) / 1000.0;
            if (!std::isnan(p.lat) && !std::isnan(p.lon))
                {
                    lat.append(t, p.lat);
                    lon.append(t, p.lon);
                }
            appendIfValid(ele, t, p.ele);
            appendIfValid(speed, t, p.speed);
            appendIfValid(distance, t, p.distance);
            appendIfValid(hr, t, p.hr);
            appendIfValid(cad, t, p.cad);
            appendIfValid(power, t, p.power);
        }
    session->removeEmptyChannels();

    // one lap is the whole recording: nothing to tell apart
    if (laps.size() > 1)
        {
            QVector<TelemetrySession::Lap> found;
            for (int i = 0; i < laps.size(); ++i)
                {
                    const double begins = double(laps.at(i).startMs - startMs) / 1000.0;
                    // as long as the file says; without that, to the next lap or the end
                    const double ends = laps.at(i).seconds > 0.0 ? begins + laps.at(i).seconds
                                        : i + 1 < laps.size() ? double(laps.at(i + 1).startMs - startMs) / 1000.0
                                        : double(points.last().utcMs - startMs) / 1000.0;
                    if (ends > begins)
                        {
                            found.append({std::max(0.0, begins), ends});
                        }
                }
            session->setLaps(found);
        }
    return session;
}
