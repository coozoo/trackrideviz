#include "vboimporter.h"

#include "channelids.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>

namespace
{
// What a known column holds: the channel, its unit, and the factor to it.
struct Known
{
    const char *column;
    const char *channel;
    const char *unit;
    double factor;
};

const Known kKnown[] = {
    {"velocity", ChannelId::Speed, "m/s", 1.0 / 3.6},
    {"velocity kmh", ChannelId::Speed, "m/s", 1.0 / 3.6},
    {"heading", ChannelId::Heading, "deg", 1.0},
    {"height", ChannelId::Elevation, "m", 1.0},
    {"vert-vel", ChannelId::VerticalSpeed, "m/s", 1.0 / 3.6},
    {"longacc", ChannelId::GForceLongitudinal, "g", 1.0},
    {"latacc", ChannelId::GForceLateral, "g", 1.0},
};

// Columns read on their own, or of no use as a channel.
bool special(const QString &column)
{
    static const QStringList names{"time", "lat", "long", "sats", "satellites", "avifileindex", "avisynctime",
                                   "avitime", "event", "dgps"};
    return names.contains(column);
}

// "093457.40" -> seconds of the day
bool timeOfDay(const QString &value, double *seconds)
{
    if (value.size() < 6)
        {
            return false;
        }
    bool okH = false, okM = false, okS = false;
    const int h = value.left(2).toInt(&okH);
    const int m = value.mid(2, 2).toInt(&okM);
    const double s = value.mid(4).toDouble(&okS);
    if (!okH || !okM || !okS || h > 23 || m > 59 || s >= 61.0)
        {
            return false;
        }
    *seconds = h * 3600.0 + m * 60.0 + s;
    return true;
}
}

TelemetrySessionPtr VboImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (error)
                {
                    *error = QCoreApplication::translate("VboImporter", "Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(&file, error);
}

TelemetrySessionPtr VboImporter::read(QIODevice *device, QString *error)
{
    const auto fail = [error](const QString &message) -> TelemetrySessionPtr
    {
        if (error)
            {
                *error = message;
            }
        return nullptr;
    };
    static const QRegularExpression created("created on\\s+(\\d{1,2})/(\\d{1,2})/(\\d{4})", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression spaces("\\s+");
    QDate date;
    QString section;
    QStringList headerNames;
    QStringList units;
    QStringList columns;
    // as read: split one by one later, a file of an hour holds millions of values
    QVector<QByteArray> rows;
    while (!device->atEnd())
        {
            const QByteArray read = device->readLine(1 << 16).trimmed();
            if (section == QLatin1String("data"))
                {
                    if (!read.isEmpty() && !(read.startsWith('[') && read.endsWith(']')))
                        {
                            // a copy of its own size: what readLine gives can hold its 64 KB
                            rows.append(QByteArray(read.constData(), read.size()));
                            continue;
                        }
                }
            const QString line = QString::fromLatin1(read);
            // a column without a unit has an empty line of its own
            if (line.isEmpty() && section != QLatin1String("channel units"))
                {
                    continue;
                }
            if (line.startsWith('[') && line.endsWith(']'))
                {
                    // the empty lines before the next section belong to none
                    while (!units.isEmpty() && units.last().isEmpty())
                        {
                            units.removeLast();
                        }
                    section = line.mid(1, line.size() - 2).trimmed().toLower();
                    continue;
                }
            if (section.isEmpty())
                {
                    const QRegularExpressionMatch match = created.match(line);
                    if (match.hasMatch() && !date.isValid())
                        {
                            date = QDate(match.captured(3).toInt(), match.captured(2).toInt(), match.captured(1).toInt());
                        }
                }
            else if (section == "header")
                {
                    headerNames << line.toLower();
                }
            else if (section == "channel units")
                {
                    units << line;
                }
            else if (section == "column names")
                {
                    columns << line.toLower().split(spaces, Qt::SkipEmptyParts);
                }
        }
    const int timeColumn = int(columns.indexOf("time"));
    const int latColumn = int(columns.indexOf("lat"));
    const int lonColumn = int(columns.indexOf("long"));
    if (columns.isEmpty() || timeColumn < 0 || rows.isEmpty())
        {
            return fail(QCoreApplication::translate("VboImporter", "Not a VBO file, or one without data."));
        }

    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(QStringLiteral("VBO"));
    // each column: where it goes, and by how much
    struct Target
    {
        Channel *channel{nullptr};
        double factor{1.0};
    };
    QVector<Target> targets(columns.size());
    for (int c = 0; c < columns.size(); ++c)
        {
            const QString &name = columns.at(c);
            if (special(name))
                {
                    continue;
                }
            const Known *known = nullptr;
            for (const Known &k : kKnown)
                {
                    known = !known && name == QLatin1String(k.column) ? &k : known;
                }
            if (known)
                {
                    if (!session->hasChannel(known->channel))
                        {
                            targets[c] = {&session->channelForWriting(known->channel, known->unit), known->factor};
                        }
                    continue;
                }
            // the header names the columns in the same order, with the units beside
            const int index = int(headerNames.indexOf(name));
            const QString unit = index >= 0 && index < units.size() ? units.at(index) : QString();
            targets[c] = {&session->channelForWriting(name, unit), 1.0};
        }
    Channel *lat = latColumn >= 0 ? &session->channelForWriting(ChannelId::PositionLat, "deg") : nullptr;
    Channel *lon = lonColumn >= 0 ? &session->channelForWriting(ChannelId::PositionLon, "deg") : nullptr;

    double first = NAN;
    double dayOffset = 0.0;
    double previous = -1.0;
    int damaged = 0;
    for (const QByteArray &line : std::as_const(rows))
        {
            const QStringList row = QString::fromLatin1(line).split(spaces, Qt::SkipEmptyParts);
            double time = 0.0;
            if (row.size() < columns.size() || !timeOfDay(row.at(timeColumn), &time))
                {
                    ++damaged;
                    continue;
                }
            if (previous >= 0.0 && time + dayOffset < previous - 43200.0)
                {
                    dayOffset += 86400.0;
                }
            time += dayOffset;
            previous = std::max(previous, time);
            if (std::isnan(first))
                {
                    first = time;
                }
            const double t = time - first;
            if (lat && lon)
                {
                    bool okLat = false, okLon = false;
                    // minutes; longitude positive to the west
                    const double la = row.at(latColumn).toDouble(&okLat) / 60.0;
                    const double lo = -row.at(lonColumn).toDouble(&okLon) / 60.0;
                    // no fix: zeros
                    if (okLat && okLon && (la != 0.0 || lo != 0.0) && std::fabs(la) <= 90.0 && std::fabs(lo) <= 180.0
                            && lat->append(t, la))
                        {
                            lon->append(t, lo);
                        }
                }
            for (int c = 0; c < columns.size(); ++c)
                {
                    if (!targets.at(c).channel)
                        {
                            continue;
                        }
                    bool ok = false;
                    const double value = row.at(c).toDouble(&ok);
                    if (ok)
                        {
                            targets.at(c).channel->append(t, value * targets.at(c).factor);
                        }
                }
        }
    session->removeEmptyChannels();
    if (session->channelIds().isEmpty())
        {
            return fail(QCoreApplication::translate("VboImporter", "The VBO file holds no data that could be read."));
        }
    if (date.isValid() && !std::isnan(first))
        {
            session->setStartUtc(QDateTime(date, QTime(0, 0), QTimeZone::utc()).addMSecs(qint64(std::llround(first * 1000.0))));
        }
    if (damaged > 0)
        {
            session->addWarning(QCoreApplication::translate("VboImporter", "%n line(s) of data could not be read.", nullptr, damaged));
        }
    return session;
}
