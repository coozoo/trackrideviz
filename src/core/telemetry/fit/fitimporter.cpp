#include "fitimporter.h"

#include "../channelids.h"
#include "fitdecoder.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace
{
// `record` message field numbers (FIT profile).
enum RecordField
{
    PositionLat = 0, PositionLong = 1, Altitude = 2, HeartRate = 3, Cadence = 4, Distance = 5,
    Speed = 6, Power = 7, Grade = 9, Temperature = 13, LeftRightBalance = 30,
    LeftTorqueEffectiveness = 43, RightTorqueEffectiveness = 44, LeftPedalSmoothness = 45,
    RightPedalSmoothness = 46, CombinedPedalSmoothness = 47, LeftPco = 67, RightPco = 68,
    LeftPowerPhase = 69, LeftPowerPhasePeak = 70, RightPowerPhase = 71, RightPowerPhasePeak = 72,
    EnhancedSpeed = 73, EnhancedAltitude = 78
};

struct KnownField
{
    int num;
    const char *channel;
    const char *unit;
    double scale;  // value = raw / scale - offset
    double offset;
};

const KnownField kKnownFields[] =
{
    {HeartRate, ChannelId::HeartRate, "bpm", 1.0, 0.0},
    {Cadence, ChannelId::Cadence, "rpm", 1.0, 0.0},
    {Distance, ChannelId::Distance, "m", 100.0, 0.0},
    {Power, ChannelId::Power, "W", 1.0, 0.0},
    {Grade, ChannelId::Grade, "%", 100.0, 0.0},
    {Temperature, ChannelId::Temperature, "C", 1.0, 0.0},
    {LeftTorqueEffectiveness, ChannelId::LeftTorqueEffectiveness, "%", 2.0, 0.0},
    {RightTorqueEffectiveness, ChannelId::RightTorqueEffectiveness, "%", 2.0, 0.0},
    {LeftPedalSmoothness, ChannelId::LeftPedalSmoothness, "%", 2.0, 0.0},
    {RightPedalSmoothness, ChannelId::RightPedalSmoothness, "%", 2.0, 0.0},
    {CombinedPedalSmoothness, ChannelId::CombinedPedalSmoothness, "%", 2.0, 0.0},
    {LeftPco, ChannelId::LeftPlatformCenterOffset, "mm", 1.0, 0.0},
    {RightPco, ChannelId::RightPlatformCenterOffset, "mm", 1.0, 0.0},
};

// Power phase fields: an array of start and end angle, 256 steps to the turn.
struct PhaseField
{
    int num;
    const char *start;
    const char *length;
};

const PhaseField kPhaseFields[] =
{
    {LeftPowerPhase, ChannelId::LeftPowerPhaseStart, ChannelId::LeftPowerPhaseLength},
    {LeftPowerPhasePeak, ChannelId::LeftPowerPhasePeakStart, ChannelId::LeftPowerPhasePeakLength},
    {RightPowerPhase, ChannelId::RightPowerPhaseStart, ChannelId::RightPowerPhaseLength},
    {RightPowerPhasePeak, ChannelId::RightPowerPhasePeakStart, ChannelId::RightPowerPhasePeakLength},
};

bool isHandledElsewhere(int num)
{
    switch (num)
        {
        case PositionLat:
        case PositionLong:
        case Altitude:
        case EnhancedAltitude:
        case Speed:
        case EnhancedSpeed:
        case LeftRightBalance:
        case Fit::TimestampField:
            return true;
        default:
            break;
        }
    for (const KnownField &known : kKnownFields)
        {
            if (known.num == num)
                {
                    return true;
                }
        }
    for (const PhaseField &phase : kPhaseFields)
        {
            if (phase.num == num)
                {
                    return true;
                }
        }
    return false;
}

struct LapTimes
{
    double start;
    double end;
};

// The 16-bit field saturates; devices that can exceed it also write the "enhanced" 32-bit one.
double preferEnhanced(const FitMessage &message, int enhancedNum, int plainNum)
{
    const double enhanced = message.value(enhancedNum);
    return std::isnan(enhanced) ? message.value(plainNum) : enhanced;
}

// Indexes of the longest run of records whose time never goes backwards.
// Real files contain a few records stamped years away from their neighbours,
// or a clock corrected in mid-recording; whatever is not on the main timeline
// is left out instead of being allowed to hide everything recorded after it.
// A NaN entry (no timestamp, or one we decided not to trust) is never kept.
QVector<bool> consistentTimeline(const QVector<double> &timestamps)
{
    const int count = timestamps.size();
    QVector<int> tails;                 // tails[k]: index ending the best run of length k + 1
    QVector<int> previous(count, -1);
    for (int i = 0; i < count; ++i)
        {
            const double t = timestamps.at(i);
            if (std::isnan(t))
                {
                    continue;
                }
            const auto it = std::upper_bound(tails.begin(), tails.end(), t, [&](double value, int index)
            {
                return value < timestamps.at(index);
            });
            const int length = int(it - tails.begin());
            previous[i] = length > 0 ? tails.at(length - 1) : -1;
            if (it == tails.end())
                {
                    tails.append(i);
                }
            else
                {
                    *it = i;
                }
        }
    QVector<bool> keep(count, false);
    for (int i = tails.isEmpty() ? -1 : tails.last(); i >= 0; i = previous.at(i))
        {
            keep[i] = true;
        }
    return keep;
}

void appendIfValid(TelemetrySession &session, const char *id, const char *unit, double time, double value)
{
    if (!std::isnan(value))
        {
            session.channelForWriting(id, unit).append(time, value);
        }
}
}

TelemetrySessionPtr FitImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = tr("Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(file.readAll(), error);
}

TelemetrySessionPtr FitImporter::read(const QByteArray &data, QString *error)
{
    // Pass 1: only the record timestamps, to decide which records are on the main timeline.
    QVector<double> timestamps;
    bool anyAbsolute = false;
    FitDecoder decoder;
    const FitDecoder::Result result = decoder.decode(data, [&](const FitMessage &message)
    {
        if (message.globalNum == Fit::Record)
            {
                const double timestamp = message.value(Fit::TimestampField);
                anyAbsolute = anyAbsolute || timestamp >= Fit::MinAbsoluteTimestamp;
                timestamps.append(timestamp);
            }
    });
    if (!result.ok)
        {
            if (error)
                {
                    *error = result.error;
                }
            return nullptr;
        }
    int recordsWithoutTime = 0;
    for (double &timestamp : timestamps)
        {
            if (std::isnan(timestamp))
                {
                    ++recordsWithoutTime;
                }
            else if (anyAbsolute && timestamp < Fit::MinAbsoluteTimestamp)
                {
                    // seconds-since-power-on stamps written before the clock was set
                    timestamp = std::nan("");
                }
        }
    const QVector<bool> keep = consistentTimeline(timestamps);
    const int recordsOffTimeline = int(keep.count(false)) - recordsWithoutTime;

    // Pass 2: build the session.
    auto session = std::make_shared<TelemetrySession>();
    bool haveStart = false;
    double startTimestamp = 0.0;
    int recordIndex = -1;
    QVector<LapTimes> laps; // raw FIT timestamps; converted once the start is known
    QVector<LapTimes> pauses;
    double timerStoppedAt = std::nan("");
    double utcOffset = std::nan("");
    double manufacturer = std::nan("");
    double product = std::nan("");
    QString productName;

    auto onRecord = [&](const FitMessage &message)
    {
        ++recordIndex;
        if (!keep.at(recordIndex))
            {
                return;
            }
        const double timestamp = message.value(Fit::TimestampField);
        if (!haveStart)
            {
                haveStart = true;
                startTimestamp = timestamp;
            }
        // Several records may share a second; Channel::append() keeps the first of them.
        const double t = timestamp - startTimestamp;

        const double lat = message.value(PositionLat);
        const double lon = message.value(PositionLong);
        // 0/0 is what several devices write while they have no fix yet
        if (!std::isnan(lat) && !std::isnan(lon) && !(lat == 0.0 && lon == 0.0))
            {
                const double semicircleToDegrees = 180.0 / 2147483648.0;
                session->channelForWriting(ChannelId::PositionLat, "deg").append(t, lat * semicircleToDegrees);
                session->channelForWriting(ChannelId::PositionLon, "deg").append(t, lon * semicircleToDegrees);
            }

        const double altitude = preferEnhanced(message, EnhancedAltitude, Altitude);
        appendIfValid(*session, ChannelId::Elevation, "m", t, altitude / 5.0 - 500.0);
        const double speed = preferEnhanced(message, EnhancedSpeed, Speed);
        appendIfValid(*session, ChannelId::Speed, "m/s", t, speed / 1000.0);

        for (const KnownField &known : kKnownFields)
            {
                appendIfValid(*session, known.channel, known.unit, t, message.value(known.num) / known.scale - known.offset);
            }

        // the right leg's share in the low 7 bits when the top bit says so; without it the side is unknown
        const double balance = message.value(LeftRightBalance);
        if (!std::isnan(balance) && (int(balance) & 0x80) && (int(balance) & 0x7F) <= 100)
            {
                const double right = int(balance) & 0x7F;
                appendIfValid(*session, ChannelId::PowerBalanceLeft, "%", t, 100.0 - right);
                appendIfValid(*session, ChannelId::PowerBalanceRight, "%", t, right);
                const double power = message.value(Power);
                appendIfValid(*session, ChannelId::LeftPower, "W", t, power * (100.0 - right) / 100.0);
                appendIfValid(*session, ChannelId::RightPower, "W", t, power * right / 100.0);
            }
        for (const PhaseField &phase : kPhaseFields)
            {
                const FitField *field = message.field(phase.num);
                if (!field || field->values.size() < 2 || std::isnan(field->values.at(0)) || std::isnan(field->values.at(1)))
                    {
                        continue;
                    }
                const double start = field->values.at(0) * 360.0 / 256.0;
                const double end = field->values.at(1) * 360.0 / 256.0;
                appendIfValid(*session, phase.start, "deg", t, start);
                appendIfValid(*session, phase.length, "deg", t, std::fmod(end - start + 360.0, 360.0));
            }

        for (const FitField &field : message.fields)
            {
                if (!field.isValid())
                    {
                        continue;
                    }
                if (field.developer)
                    {
                        const QString name = field.name.isEmpty()
                                             ? QString("%1.%2").arg(field.developerIndex).arg(field.num)
                                             : field.name;
                        session->channelForWriting("dev." + name, field.units).append(t, field.value());
                    }
                else if (!isHandledElsewhere(field.num))
                    {
                        session->channelForWriting(QString("fit.record.%1").arg(field.num), QString()).append(t, field.value());
                    }
            }
    };

    decoder.decode(data, [&](const FitMessage &message)
    {
        switch (message.globalNum)
            {
            case Fit::Record:
                onRecord(message);
                break;
            case Fit::Lap:
            {
                // 253 timestamp = lap end, 2 start_time
                const double end = message.value(Fit::TimestampField);
                const double start = message.value(2);
                if (!std::isnan(start) && !std::isnan(end) && end > start)
                    {
                        laps.append({start, end});
                    }
                break;
            }
            case Fit::Event:
            {
                // 0 event (0 = timer), 1 event_type (0 start, 1 stop, 4 stop_all)
                if (message.value(0) != 0.0)
                    {
                        break;
                    }
                const double when = message.value(Fit::TimestampField);
                const double type = message.value(1);
                if ((type == 1.0 || type == 4.0) && std::isnan(timerStoppedAt))
                    {
                        timerStoppedAt = when;
                    }
                else if (type == 0.0 && !std::isnan(timerStoppedAt))
                    {
                        if (when > timerStoppedAt)
                            {
                                pauses.append({timerStoppedAt, when});
                            }
                        timerStoppedAt = std::nan("");
                    }
                break;
            }
            case Fit::Activity:
            {
                // 5 local_timestamp: the same instant as `timestamp`, on the wall clock
                const double offset = message.value(5) - message.value(Fit::TimestampField);
                if (!std::isnan(offset) && std::fabs(offset) <= 14 * 3600)
                    {
                        utcOffset = offset;
                    }
                break;
            }
            case Fit::FileId:
                // 1 manufacturer, 2 product, 8 product_name
                manufacturer = message.value(1);
                product = message.value(2);
                if (const FitField *name = message.field(8))
                    {
                        productName = name->text();
                    }
                break;
            default:
                break;
            }
    });

    if (!haveStart)
        {
            if (error)
                {
                    *error = tr("The FIT file contains no timed records. It may be a settings, course or workout file, not an activity.");
                }
            return nullptr;
        }

    for (const QString &warning : result.warnings)
        {
            session->addWarning(warning);
        }
    if (recordsWithoutTime > 0)
        {
            session->addWarning(tr("%1 records without a timestamp were skipped.").arg(recordsWithoutTime));
        }
    if (recordsOffTimeline > 0)
        {
            session->addWarning(tr("%1 records with a timestamp that does not fit the rest of the recording were skipped.").arg(recordsOffTimeline));
        }

    if (startTimestamp >= Fit::MinAbsoluteTimestamp)
        {
            session->setStartUtc(QDateTime::fromSecsSinceEpoch(qint64(startTimestamp) + Fit::EpochOffset).toUTC());
        }
    else
        {
            session->addWarning(tr("The file has no absolute time; it has to be synchronized with the video manually."));
        }

    const double duration = session->duration();
    for (const LapTimes &lap : laps)
        {
            TelemetrySession::Lap l;
            l.startTime = qBound(0.0, lap.start - startTimestamp, duration);
            l.endTime = qBound(0.0, lap.end - startTimestamp, duration);
            if (l.endTime > l.startTime)
                {
                    session->addLap(l);
                }
        }

    for (const LapTimes &pause : pauses)
        {
            TelemetrySession::Pause p;
            p.startTime = qBound(0.0, pause.start - startTimestamp, duration);
            p.endTime = qBound(0.0, pause.end - startTimestamp, duration);
            if (p.endTime > p.startTime)
                {
                    session->addPause(p);
                }
        }
    if (!std::isnan(utcOffset))
        {
            session->setUtcOffsetSeconds(int(utcOffset));
        }

    QString source = "FIT";
    if (!productName.isEmpty())
        {
            source += ", " + productName;
        }
    else if (!std::isnan(manufacturer))
        {
            source += QString(", manufacturer %1").arg(int(manufacturer));
            if (!std::isnan(product))
                {
                    source += QString(" product %1").arg(int(product));
                }
        }
    session->setSourceInfo(source);
    return session;
}
