#include "nmeaimporter.h"

#include "channelids.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimeZone>

#include <cmath>
#include <map>
#include <set>

namespace
{
// The text between '$' and '*' when the checksum after it matches, or there is none.
bool sentence(const QByteArray &line, QByteArray *body)
{
    const int start = line.indexOf('$');
    if (start < 0)
        {
            return false;
        }
    const int star = line.indexOf('*', start);
    *body = line.mid(start + 1, star < 0 ? -1 : star - start - 1).trimmed();
    if (star < 0)
        {
            return !body->isEmpty();
        }
    bool ok = false;
    const int expected = line.mid(star + 1, 2).toInt(&ok, 16);
    if (!ok)
        {
            return false;
        }
    quint8 sum = 0;
    for (const char c : *body)
        {
            sum ^= quint8(c);
        }
    return sum == expected;
}

// "4916.45", "N" -> 49.274166..., by the hemisphere's sign
bool coordinate(const QByteArray &value, const QByteArray &hemisphere, double *degrees)
{
    bool ok = false;
    const double raw = value.toDouble(&ok);
    if (!ok || value.isEmpty())
        {
            return false;
        }
    const double whole = std::floor(raw / 100.0);
    *degrees = whole + (raw - whole * 100.0) / 60.0;
    if (hemisphere == "S" || hemisphere == "W")
        {
            *degrees = -*degrees;
        }
    return true;
}

// "123519.25" -> seconds of the day
bool timeOfDay(const QByteArray &value, double *seconds)
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

struct Fix
{
    double lat{NAN};
    double lon{NAN};
    double ele{NAN};
    double speed{NAN};      // m/s
    double course{NAN};     // degrees
};

const double kKnot = 1852.0 / 3600.0;   // m/s
}

bool NmeaImporter::looksLikeNmea(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }
    // a few lines in, past a header a logger may write
    for (int i = 0; i < 20 && !file.atEnd(); ++i)
        {
            const QByteArray line = file.readLine(4096).trimmed();
            QByteArray body;
            if (line.startsWith('$') && line.size() > 6 && sentence(line, &body)
                    && (body.mid(2, 3) == "RMC" || body.mid(2, 3) == "GGA"))
                {
                    return true;
                }
        }
    return false;
}

TelemetrySessionPtr NmeaImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = QCoreApplication::translate("NmeaImporter", "Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(&file, error);
}

TelemetrySessionPtr NmeaImporter::read(QIODevice *device, QString *error)
{
    // by the moment, as the receiver stamps it: seconds counted on over midnight
    std::map<double, Fix> fixes;
    std::set<double> noFix;     // moments a sentence says had none, whatever another says
    QDate date;                 // of the first RMC
    double dayOffset = 0.0;     // days after the first, in seconds
    double previous = -1.0;
    int damaged = 0;
    while (!device->atEnd())
        {
            const QByteArray line = device->readLine(4096);
            QByteArray body;
            if (!line.contains('$'))
                {
                    continue;
                }
            if (!sentence(line, &body))
                {
                    ++damaged;
                    continue;
                }
            const QList<QByteArray> f = body.split(',');
            const QByteArray type = f.value(0).mid(2);
            double time = 0.0;
            if ((type != "RMC" && type != "GGA") || !timeOfDay(f.value(1), &time))
                {
                    continue;
                }
            // the clock went back by more than half a day: the next one began
            if (previous >= 0.0 && time + dayOffset < previous - 43200.0)
                {
                    dayOffset += 86400.0;
                }
            time += dayOffset;
            previous = std::max(previous, time);
            if (noFix.count(time))
                {
                    continue;
                }
            Fix &fix = fixes[time];
            if (type == "RMC" && f.size() >= 10)
                {
                    // A: valid, V: no fix
                    if (f.at(2) != "A" || !coordinate(f.at(3), f.at(4), &fix.lat) || !coordinate(f.at(5), f.at(6), &fix.lon))
                        {
                            fixes.erase(time);
                            noFix.insert(time);
                            continue;
                        }
                    bool ok = false;
                    const double knots = f.at(7).toDouble(&ok);
                    fix.speed = ok ? knots * kKnot : NAN;
                    const double course = f.at(8).toDouble(&ok);
                    fix.course = ok && !f.at(8).isEmpty() ? course : NAN;
                    if (!date.isValid())
                        {
                            const QDate day = QDate::fromString(QString::fromLatin1(f.at(9)), "ddMMyy");
                            if (day.isValid())
                                {
                                    // two digits: this century
                                    date = day.addYears(day.year() < 2000 ? 100 : 0);
                                    // the day of the first moment
                                    date = date.addDays(-qint64(dayOffset / 86400.0));
                                }
                        }
                }
            else if (type == "GGA" && f.size() >= 10)
                {
                    // fix quality 0: none
                    if (f.at(6).toInt() == 0)
                        {
                            fixes.erase(time);
                            noFix.insert(time);
                            continue;
                        }
                    double lat = NAN;
                    double lon = NAN;
                    if (std::isnan(fix.lat) && coordinate(f.at(2), f.at(3), &lat) && coordinate(f.at(4), f.at(5), &lon))
                        {
                            fix.lat = lat;
                            fix.lon = lon;
                        }
                    bool ok = false;
                    const double height = f.at(9).toDouble(&ok);
                    if (ok && !f.at(9).isEmpty())
                        {
                            fix.ele = height;
                        }
                }
        }
    // moments with a position
    for (auto it = fixes.begin(); it != fixes.end();)
        {
            it = std::isnan(it->second.lat) || std::isnan(it->second.lon) ? fixes.erase(it) : std::next(it);
        }
    if (fixes.empty())
        {
            if (error)
                {
                    *error = QCoreApplication::translate("NmeaImporter", "No positions with a fix in this NMEA file.");
                }
            return nullptr;
        }
    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(QStringLiteral("NMEA"));
    const double first = fixes.begin()->first;
    if (date.isValid())
        {
            session->setStartUtc(QDateTime(date, QTime(0, 0), QTimeZone::utc()).addMSecs(qint64(std::llround(first * 1000.0))));
        }
    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &ele = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    Channel &heading = session->channelForWriting(ChannelId::Heading, "deg");
    for (const auto &[time, fix] : fixes)
        {
            const double t = time - first;
            lat.append(t, fix.lat);
            lon.append(t, fix.lon);
            if (!std::isnan(fix.ele))
                {
                    ele.append(t, fix.ele);
                }
            if (!std::isnan(fix.speed))
                {
                    speed.append(t, fix.speed);
                }
            if (!std::isnan(fix.course))
                {
                    heading.append(t, fix.course);
                }
        }
    session->removeEmptyChannels();
    if (damaged > 0)
        {
            session->addWarning(QCoreApplication::translate("NmeaImporter", "%n line(s) with a wrong checksum were left out.",
                                                            nullptr, damaged));
        }
    return session;
}
