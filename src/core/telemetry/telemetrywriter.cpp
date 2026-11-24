#include "telemetrywriter.h"

#include "channelids.h"
#include "datatypes.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
// `time` seconds into the recording, as GPX and the CSV's clock column write it.
QString utcText(const TelemetrySession &session, double time)
{
    return session.startUtc().addMSecs(qint64(std::llround(time * 1000.0))).toUTC().toString(Qt::ISODateWithMs);
}

QString number(double value, int decimals = -1)
{
    return decimals >= 0 ? QString::number(value, 'f', decimals) : QString::number(value, 'g', 10);
}
}

QByteArray TelemetryWriter::gpx(const TelemetrySession &session)
{
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeStartElement("gpx");
    xml.writeAttribute("version", "1.1");
    xml.writeAttribute("creator", QCoreApplication::applicationName());
    xml.writeDefaultNamespace("http://www.topografix.com/GPX/1/1");
    xml.writeNamespace("http://www.garmin.com/xmlschemas/TrackPointExtension/v2", "gpxtpx");
    if (session.startUtc().isValid())
        {
            xml.writeStartElement("metadata");
            xml.writeTextElement("time", utcText(session, 0.0));
            xml.writeEndElement();
        }
    xml.writeStartElement("trk");
    xml.writeStartElement("trkseg");
    const Channel *lat = session.channel(ChannelId::PositionLat);
    const Channel *lon = session.channel(ChannelId::PositionLon);
    const Channel *elevation = session.channel(ChannelId::Elevation);
    // in the order Garmin's extension wants them
    const QVector<QPair<const char *, const Channel *>> extensions
    {
        {"gpxtpx:atemp", session.channel(ChannelId::Temperature)},
        {"gpxtpx:hr", session.channel(ChannelId::HeartRate)},
        {"gpxtpx:cad", session.channel(ChannelId::Cadence)},
        {"gpxtpx:speed", session.channel(ChannelId::Speed)},
    };
    const Channel *power = session.channel(ChannelId::Power);
    for (int i = 0; lat && lon && i < lat->count(); ++i)
        {
            const double t = lat->timeAt(i);
            bool ok = false;
            const double longitude = lon->valueAt(t, &ok);
            if (!ok)
                {
                    continue;
                }
            xml.writeStartElement("trkpt");
            xml.writeAttribute("lat", number(lat->valueAtIndex(i), 7));
            xml.writeAttribute("lon", number(longitude, 7));
            const double height = elevation ? elevation->valueAt(t, &ok) : 0.0;
            if (elevation && ok)
                {
                    xml.writeTextElement("ele", number(height, 2));
                }
            if (session.startUtc().isValid())
                {
                    xml.writeTextElement("time", utcText(session, t));
                }
            // what there is at this moment
            QVector<QPair<const char *, double>> values;
            for (const auto &[name, channel] : extensions)
                {
                    double value = channel ? channel->valueAt(t, &ok) : 0.0;
                    if (channel && ok)
                        {
                            // Garmin's cadence of a run is one leg's: strides, not steps
                            if (channel->id() == QLatin1String(ChannelId::Cadence) && channel->unit() == QLatin1String("spm"))
                                {
                                    value /= 2.0;
                                }
                            values.append({name, value});
                        }
                }
            const double watts = power ? power->valueAt(t, &ok) : 0.0;
            const bool hasPower = power && ok;
            if (!values.isEmpty() || hasPower)
                {
                    xml.writeStartElement("extensions");
                    if (hasPower)
                        {
                            xml.writeTextElement("power", number(std::round(watts), 0));
                        }
                    if (!values.isEmpty())
                        {
                            xml.writeStartElement("gpxtpx:TrackPointExtension");
                            for (const auto &[name, value] : values)
                                {
                                    const bool whole = qstrcmp(name, "gpxtpx:hr") == 0 || qstrcmp(name, "gpxtpx:cad") == 0;
                                    xml.writeTextElement(name, whole ? number(std::round(value), 0) : number(value, 2));
                                }
                            xml.writeEndElement();
                        }
                    xml.writeEndElement();
                }
            xml.writeEndElement();
        }
    xml.writeEndElement(); // trkseg
    xml.writeEndElement(); // trk
    xml.writeEndElement(); // gpx
    xml.writeEndDocument();
    return out;
}

QByteArray TelemetryWriter::csv(const TelemetrySession &session)
{
    QVector<const Channel *> channels;
    for (const QString &id : session.channelIds())
        {
            const Channel *channel = session.channel(id);
            if (channel && !channel->isEmpty())
                {
                    channels.append(channel);
                }
        }
    const bool clock = session.startUtc().isValid();
    QStringList header{"time"};
    if (clock)
        {
            header << "utc";
        }
    for (const Channel *channel : std::as_const(channels))
        {
            // the separator cannot be in a name; a unit other than the one the channel usually has
            // goes after it, "cadence (spm)", or it would be read back in that one. A name that ends
            // in brackets of its own (a device's "Power (W)") gets them always, empty if need be,
            // or its own would be read as the unit
            static const QRegularExpression endsInBrackets("\\S\\s+\\([^()]*\\)$");
            QString name = QString(channel->id()).replace(',', ' ');
            const QString unit = QString(channel->unit()).replace(',', ' ').remove('(').remove(')').trimmed();
            if ((!unit.isEmpty() && unit != DataTypes::storedUnitOf(channel->id())) || endsInBrackets.match(name).hasMatch())
                {
                    name += QString(" (%1)").arg(unit);
                }
            header << name;
        }
    QByteArray out = QByteArray(CsvMark) + "\n" + header.join(',').toUtf8() + "\n";

    // every channel walked along at once: a row for each moment any of them has
    QVector<int> next(channels.size(), 0);
    for (;;)
        {
            double t = std::numeric_limits<double>::infinity();
            for (int c = 0; c < channels.size(); ++c)
                {
                    if (next[c] < channels[c]->count())
                        {
                            t = std::min(t, channels[c]->timeAt(next[c]));
                        }
                }
            if (!std::isfinite(t))
                {
                    break;
                }
            QStringList row{number(t)};
            if (clock)
                {
                    row << utcText(session, t);
                }
            for (int c = 0; c < channels.size(); ++c)
                {
                    const Channel *channel = channels[c];
                    if (next[c] < channel->count() && channel->timeAt(next[c]) == t)
                        {
                            row << number(channel->valueAtIndex(next[c]));
                            ++next[c];
                        }
                    else
                        {
                            row << QString();
                        }
                }
            out += row.join(',').toUtf8() + "\n";
        }
    return out;
}

bool TelemetryWriter::save(const TelemetrySession &session, const QString &path, QString *error)
{
    const bool isGpx = QFileInfo(path).suffix().compare("gpx", Qt::CaseInsensitive) == 0;
    if (isGpx && (!session.channel(ChannelId::PositionLat) || !session.channel(ChannelId::PositionLon)))
        {
            if (error)
                {
                    *error = tr("The data has no positions: a GPX file would be empty. Save it as CSV.");
                }
            return false;
        }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(isGpx ? gpx(session) : csv(session)) < 0 || !file.commit())
        {
            if (error)
                {
                    *error = file.errorString();
                }
            return false;
        }
    return true;
}
