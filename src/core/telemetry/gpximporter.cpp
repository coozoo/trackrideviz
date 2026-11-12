#include "gpximporter.h"

#include "channelids.h"
#include "geo.h"

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
    double hr{kNoValue};
    double cad{kNoValue};
    double temp{kNoValue};
    double power{kNoValue};
    double speed{kNoValue};
    double course{kNoValue};
};

double toDouble(const QString &text)
{
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    return ok ? v : kNoValue;
}

// Reads the children of <extensions>, at any depth: vendors nest them differently.
void readExtensions(QXmlStreamReader &xml, Point &point)
{
    int depth = 1;
    while (depth > 0 && !xml.atEnd())
        {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::EndElement)
                {
                    --depth;
                    continue;
                }
            if (token != QXmlStreamReader::StartElement)
                {
                    continue;
                }
            const QString name = xml.name().toString().toLower();
            double *target = nullptr;
            if (name == "hr" || name == "heartrate")
                {
                    target = &point.hr;
                }
            else if (name == "cad" || name == "cadence")
                {
                    target = &point.cad;
                }
            else if (name == "atemp" || name == "temp")
                {
                    target = &point.temp;
                }
            else if (name == "power" || name == "powerinwatts" || name == "watts")
                {
                    target = &point.power;
                }
            else if (name == "speed")
                {
                    target = &point.speed;
                }
            else if (name == "course" || name == "bearing")
                {
                    target = &point.course;
                }

            if (target)
                {
                    // consumes the matching end element as well
                    *target = toDouble(xml.readElementText(QXmlStreamReader::SkipChildElements));
                }
            else
                {
                    ++depth;
                }
        }
}

Point readTrackPoint(QXmlStreamReader &xml)
{
    Point point;
    point.lat = toDouble(xml.attributes().value("lat").toString());
    point.lon = toDouble(xml.attributes().value("lon").toString());

    while (xml.readNextStartElement())
        {
            const QString name = xml.name().toString().toLower();
            if (name == "ele")
                {
                    point.ele = toDouble(xml.readElementText());
                }
            else if (name == "time")
                {
                    const QDateTime time = QDateTime::fromString(xml.readElementText().trimmed(), Qt::ISODateWithMs);
                    if (time.isValid())
                        {
                            point.utcMs = time.toMSecsSinceEpoch();
                            point.hasTime = true;
                        }
                }
            else if (name == "speed") // GPX 1.0 has it directly in the point
                {
                    point.speed = toDouble(xml.readElementText());
                }
            else if (name == "course")
                {
                    point.course = toDouble(xml.readElementText());
                }
            else if (name == "extensions")
                {
                    readExtensions(xml, point);
                }
            else
                {
                    xml.skipCurrentElement();
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

TelemetrySessionPtr GpxImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = QCoreApplication::translate("GpxImporter", "Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(&file, error);
}

TelemetrySessionPtr GpxImporter::read(QIODevice *device, QString *error)
{
    QXmlStreamReader xml(device);
    QVector<Point> points;
    QString creator;
    int untimed = 0;

    while (!xml.atEnd())
        {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                {
                    continue;
                }
            const QString name = xml.name().toString().toLower();
            if (name == "gpx")
                {
                    creator = xml.attributes().value("creator").toString();
                }
            else if (name == "trkpt")
                {
                    const Point point = readTrackPoint(xml);
                    if (xml.hasError())
                        {
                            // the file ends inside this point; what we have of it is not reliable
                            break;
                        }
                    if (!point.hasTime)
                        {
                            ++untimed;
                        }
                    else if (!std::isnan(point.lat) && !std::isnan(point.lon))
                        {
                            points.append(point);
                        }
                }
        }

    // A file cut off in the middle (camera lost power) still has usable points before the error.
    if (xml.hasError() && points.isEmpty())
        {
            if (error)
                {
                    *error = QCoreApplication::translate("GpxImporter", "Not a valid GPX file: %1 (line %2)")
                             .arg(xml.errorString()).arg(xml.lineNumber());
                }
            return nullptr;
        }
    if (points.isEmpty())
        {
            if (error)
                {
                    *error = untimed > 0
                             ? QCoreApplication::translate("GpxImporter", "The GPX track has no timestamps, so it cannot be synchronized with video.")
                             : QCoreApplication::translate("GpxImporter", "The GPX file contains no track points.");
                }
            return nullptr;
        }

    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(creator.isEmpty() ? QString("GPX") : QString("GPX, %1").arg(creator));
    if (xml.hasError())
        {
            session->addWarning(QCoreApplication::translate("GpxImporter", "The file is damaged after line %1; only the points before it were read.")
                                .arg(xml.lineNumber()));
        }
    if (untimed > 0)
        {
            session->addWarning(QCoreApplication::translate("GpxImporter", "%1 points without a timestamp were skipped.").arg(untimed));
        }

    const qint64 startMs = points.first().utcMs;
    session->setStartUtc(QDateTime::fromMSecsSinceEpoch(startMs).toUTC());

    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &ele = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    Channel &distance = session->channelForWriting(ChannelId::Distance, "m");
    Channel &heading = session->channelForWriting(ChannelId::Heading, "deg");
    Channel &hr = session->channelForWriting(ChannelId::HeartRate, "bpm");
    Channel &cad = session->channelForWriting(ChannelId::Cadence, "rpm");
    Channel &power = session->channelForWriting(ChannelId::Power, "W");
    Channel &temp = session->channelForWriting(ChannelId::Temperature, "C");

    bool fileHasSpeed = false;
    for (const Point &p : points)
        {
            if (!std::isnan(p.speed))
                {
                    fileHasSpeed = true;
                    break;
                }
        }

    double travelled = 0.0;
    double prevTime = 0.0;
    const Point *prev = nullptr;
    int outOfOrder = 0;
    for (const Point &p : points)
        {
            const double t = (p.utcMs - startMs) / 1000.0;
            // lat decides whether the point is accepted; the other channels follow it
            if (!lat.append(t, p.lat))
                {
                    ++outOfOrder;
                    continue;
                }
            lon.append(t, p.lon);
            appendIfValid(ele, t, p.ele);
            appendIfValid(hr, t, p.hr);
            appendIfValid(cad, t, p.cad);
            appendIfValid(power, t, p.power);
            appendIfValid(temp, t, p.temp);
            appendIfValid(heading, t, p.course);

            double step = 0.0;
            if (prev)
                {
                    step = Geo::distanceMeters(prev->lat, prev->lon, p.lat, p.lon);
                    travelled += step;
                }
            distance.append(t, travelled);
            if (fileHasSpeed)
                {
                    appendIfValid(speed, t, p.speed);
                }
            else if (prev)
                {
                    speed.append(t, step / (t - prevTime));
                }
            prev = &p;
            prevTime = t;
        }
    if (outOfOrder > 0)
        {
            session->addWarning(QCoreApplication::translate("GpxImporter", "%1 points with repeated or backward time were skipped.").arg(outOfOrder));
        }

    session->removeEmptyChannels();
    return session;
}
