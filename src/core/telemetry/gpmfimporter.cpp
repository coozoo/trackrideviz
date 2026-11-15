#include "gpmfimporter.h"

#include "channelids.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimeZone>
#include <QtEndian>

#include <cmath>

namespace
{
quint32 fourcc(const char *name)
{
    return qFromBigEndian<quint32>(name);
}

quint32 u32(const QByteArray &data, qint64 at)
{
    return at + 4 <= data.size() ? qFromBigEndian<quint32>(data.constData() + at) : 0;
}

quint64 u64(const QByteArray &data, qint64 at)
{
    return at + 8 <= data.size() ? qFromBigEndian<quint64>(data.constData() + at) : 0;
}

// --- the MP4 around the data ------------------------------------------------

struct Box
{
    quint32 type{0};
    qint64 start{0};   // of its contents
    qint64 size{0};    // of its contents
};

// The boxes directly inside a stretch of `data`.
QVector<Box> boxesIn(const QByteArray &data, qint64 from, qint64 length)
{
    QVector<Box> boxes;
    qint64 at = from;
    const qint64 end = std::min<qint64>(from + length, data.size());
    while (at + 8 <= end)
        {
            qint64 size = u32(data, at);
            qint64 header = 8;
            if (size == 1)
                {
                    size = qint64(u64(data, at + 8));
                    header = 16;
                }
            else if (size == 0)
                {
                    size = end - at;
                }
            if (size < header || at + size > end)
                {
                    break;
                }
            boxes.append({u32(data, at + 4), at + header, size - header});
            at += size;
        }
    return boxes;
}

bool find(const QByteArray &data, const Box &parent, const char *type, Box *found)
{
    for (const Box &box : boxesIn(data, parent.start, parent.size))
        {
            if (box.type == fourcc(type))
                {
                    *found = box;
                    return true;
                }
        }
    return false;
}

// Where each sample of a track is in the file, how long it is there and on the clock.
struct Sample
{
    qint64 offset{0};
    qint64 size{0};
    double start{0.0};
    double duration{0.0};
};

// The samples of the "gpmd" track of a `moov` box; empty when it has none.
QVector<Sample> dataSamples(const QByteArray &moov)
{
    for (const Box &track : boxesIn(moov, 0, moov.size()))
        {
            Box mdia, mdhd, minf, stbl, stsd, stts, stsc, stsz, chunks;
            if (track.type != fourcc("trak") || !find(moov, track, "mdia", &mdia) || !find(moov, mdia, "mdhd", &mdhd)
                    || !find(moov, mdia, "minf", &minf) || !find(moov, minf, "stbl", &stbl)
                    || !find(moov, stbl, "stsd", &stsd) || !find(moov, stbl, "stts", &stts)
                    || !find(moov, stbl, "stsc", &stsc) || !find(moov, stbl, "stsz", &stsz))
                {
                    continue;
                }
            // the kind of the track's samples: after version/flags, the count, and the entry's size
            if (u32(moov, stsd.start + 12) != fourcc("gpmd"))
                {
                    continue;
                }
            const bool wide = find(moov, stbl, "co64", &chunks);
            if (!wide && !find(moov, stbl, "stco", &chunks))
                {
                    continue;
                }
            const bool longTimes = quint8(moov.at(mdhd.start)) == 1;
            const double timescale = std::max<quint32>(1, u32(moov, mdhd.start + (longTimes ? 20 : 12)));

            // sizes
            const quint32 sameSize = u32(moov, stsz.start + 4);
            const quint32 count = u32(moov, stsz.start + 8);
            QVector<Sample> samples;
            if (count > 10000000)
                {
                    return samples;
                }
            samples.resize(count);
            for (quint32 i = 0; i < count; ++i)
                {
                    samples[i].size = sameSize ? sameSize : u32(moov, stsz.start + 12 + 4 * qint64(i));
                }
            // times: runs of samples of one length
            const quint32 runs = u32(moov, stts.start + 4);
            double clock = 0.0;
            quint32 index = 0;
            for (quint32 r = 0; r < runs && index < count; ++r)
                {
                    const quint32 length = u32(moov, stts.start + 8 + 8 * qint64(r));
                    const double each = u32(moov, stts.start + 12 + 8 * qint64(r)) / timescale;
                    for (quint32 k = 0; k < length && index < count; ++k, ++index)
                        {
                            samples[index].start = clock;
                            samples[index].duration = each;
                            clock += each;
                        }
                }
            // places: chunks of the file, each holding some samples one after another
            const quint32 chunkCount = u32(moov, chunks.start + 4);
            const quint32 groups = u32(moov, stsc.start + 4);
            index = 0;
            for (quint32 g = 0; g < groups && index < count; ++g)
                {
                    const quint32 first = u32(moov, stsc.start + 8 + 12 * qint64(g));
                    const quint32 perChunk = u32(moov, stsc.start + 12 + 12 * qint64(g));
                    const quint32 next = g + 1 < groups ? u32(moov, stsc.start + 8 + 12 * qint64(g + 1)) : chunkCount + 1;
                    for (quint32 chunk = first; chunk < next && chunk <= chunkCount && index < count; ++chunk)
                        {
                            qint64 at = wide ? qint64(u64(moov, chunks.start + 8 + 8 * qint64(chunk - 1)))
                                        : qint64(u32(moov, chunks.start + 8 + 4 * qint64(chunk - 1)));
                            for (quint32 k = 0; k < perChunk && index < count; ++k, ++index)
                                {
                                    samples[index].offset = at;
                                    at += samples[index].size;
                                }
                        }
                }
            samples.resize(index);
            return samples;
        }
    return {};
}

// --- the data itself --------------------------------------------------------

struct Fix
{
    double time{0.0};   // s on the video's clock
    double lat{0.0};
    double lon{0.0};
    double ele{0.0};
    double speed{0.0};  // over the ground, m/s
};

struct Reading
{
    QVector<Fix> fixes;
    QDateTime firstUtc;         // the clock at ...
    double firstUtcTime{0.0};   // ... this moment of the video
};

// What one stream of a payload says about itself before its samples come.
struct Stream
{
    QVector<double> scale;
    int fix{-1};          // GPSF: 0 none, 2 or 3; -1 not said
    QDateTime utc;        // GPSU
};

double scaled(qint32 raw, const QVector<double> &scale, int column)
{
    const double by = column < scale.size() ? scale.at(column) : scale.isEmpty() ? 1.0 : scale.last();
    return by != 0.0 ? raw / by : raw;
}

// One level of records between `at` and `end`; nested levels are read by calling itself.
void readRecords(const QByteArray &data, qint64 at, qint64 end, const GpmfImporter::Payload &payload, Stream stream,
                 Reading *reading)
{
    while (at + 8 <= end)
        {
            const quint32 key = u32(data, at);
            const char type = data.at(at + 4);
            const int size = quint8(data.at(at + 5));
            const int repeat = qFromBigEndian<quint16>(data.constData() + at + 6);
            const qint64 length = qint64(size) * repeat;
            const qint64 body = at + 8;
            if (key == 0 || body + length > end)
                {
                    return;
                }
            if (type == 0)
                {
                    // a device or one of its streams: what a stream says holds inside it only
                    readRecords(data, body, body + length, payload, key == fourcc("STRM") ? Stream() : stream, reading);
                }
            else if (key == fourcc("SCAL"))
                {
                    stream.scale.clear();
                    for (int i = 0; i < repeat; ++i)
                        {
                            stream.scale.append(size == 2 ? double(qFromBigEndian<qint16>(data.constData() + body + 2 * i))
                                                : size == 4 ? double(qFromBigEndian<qint32>(data.constData() + body + 4 * i)) : 1.0);
                        }
                }
            else if (key == fourcc("GPSF") && length >= 4)
                {
                    stream.fix = int(u32(data, body));
                }
            else if (key == fourcc("GPSU") && length >= 16)
                {
                    // "yymmddhhmmss.sss"
                    const QString text = QString::fromLatin1(data.constData() + body, 16);
                    QDateTime utc = QDateTime::fromString("20" + text.left(12), "yyyyMMddHHmmss");
                    utc.setTimeZone(QTimeZone::UTC);
                    stream.utc = utc.isValid() ? utc.addMSecs(qint64(text.mid(13, 3).toInt())) : QDateTime();
                }
            else if (key == fourcc("GPS5") && size == 20 && repeat > 0)
                {
                    // the fix is said once for all of them
                    if (stream.fix != 0)
                        {
                            for (int i = 0; i < repeat; ++i)
                                {
                                    const char *row = data.constData() + body + 20 * qint64(i);
                                    Fix fix;
                                    fix.time = payload.start + payload.duration * i / repeat;
                                    fix.lat = scaled(qFromBigEndian<qint32>(row), stream.scale, 0);
                                    fix.lon = scaled(qFromBigEndian<qint32>(row + 4), stream.scale, 1);
                                    fix.ele = scaled(qFromBigEndian<qint32>(row + 8), stream.scale, 2);
                                    fix.speed = scaled(qFromBigEndian<qint32>(row + 12), stream.scale, 3);
                                    reading->fixes.append(fix);
                                }
                            if (!reading->firstUtc.isValid() && stream.utc.isValid())
                                {
                                    reading->firstUtc = stream.utc;
                                    reading->firstUtcTime = payload.start;
                                }
                        }
                }
            else if (key == fourcc("GPS9") && size == 32 && repeat > 0)
                {
                    for (int i = 0; i < repeat; ++i)
                        {
                            const char *row = data.constData() + body + 32 * qint64(i);
                            // each with its own fix: none, 2D or 3D
                            if (qFromBigEndian<quint16>(row + 30) == 0)
                                {
                                    continue;
                                }
                            Fix fix;
                            fix.time = payload.start + payload.duration * i / repeat;
                            fix.lat = scaled(qFromBigEndian<qint32>(row), stream.scale, 0);
                            fix.lon = scaled(qFromBigEndian<qint32>(row + 4), stream.scale, 1);
                            fix.ele = scaled(qFromBigEndian<qint32>(row + 8), stream.scale, 2);
                            fix.speed = scaled(qFromBigEndian<qint32>(row + 12), stream.scale, 3);
                            reading->fixes.append(fix);
                            if (!reading->firstUtc.isValid())
                                {
                                    // days since 2000 and seconds of the day
                                    const double days = scaled(qFromBigEndian<qint32>(row + 20), stream.scale, 5);
                                    const double seconds = scaled(qFromBigEndian<qint32>(row + 24), stream.scale, 6);
                                    reading->firstUtc = QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::UTC)
                                                        .addDays(qint64(days)).addMSecs(qint64(seconds * 1000.0));
                                    reading->firstUtcTime = fix.time;
                                }
                        }
                }
            // records are padded to whole words
            at = body + ((length + 3) & ~qint64(3));
        }
}
}

TelemetrySessionPtr GpmfImporter::fromPayloads(const QVector<Payload> &payloads)
{
    Reading reading;
    for (const Payload &payload : payloads)
        {
            readRecords(payload.data, 0, payload.data.size(), payload, Stream(), &reading);
        }
    if (reading.fixes.isEmpty())
        {
            return nullptr;
        }
    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(QStringLiteral("GoPro"));
    if (reading.firstUtc.isValid())
        {
            // the clock when the video began
            session->setStartUtc(reading.firstUtc.addMSecs(-qint64(reading.firstUtcTime * 1000.0)));
        }
    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &ele = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    for (const Fix &fix : std::as_const(reading.fixes))
        {
            // a position at 0, 0 is the camera's way of having none
            if (std::fabs(fix.lat) < 1e-9 && std::fabs(fix.lon) < 1e-9)
                {
                    continue;
                }
            if (lat.append(fix.time, fix.lat))
                {
                    lon.append(fix.time, fix.lon);
                    ele.append(fix.time, fix.ele);
                    speed.append(fix.time, fix.speed);
                }
        }
    if (lat.isEmpty())
        {
            return nullptr;
        }
    return session;
}

TelemetrySessionPtr GpmfImporter::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = QCoreApplication::translate("GpmfImporter", "Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    return read(&file, error);
}

TelemetrySessionPtr GpmfImporter::read(QIODevice *device, QString *error)
{
    const auto fail = [error](const QString &message) -> TelemetrySessionPtr
    {
        if (error)
            {
                *error = message;
            }
        return nullptr;
    };
    // the index of the file: among its outermost boxes, wherever the camera put it
    QByteArray moov;
    qint64 at = 0;
    while (device->seek(at))
        {
            const QByteArray header = device->read(16);
            if (header.size() < 8)
                {
                    break;
                }
            qint64 size = u32(header, 0);
            qint64 headerSize = 8;
            if (size == 1 && header.size() >= 16)
                {
                    size = qint64(u64(header, 8));
                    headerSize = 16;
                }
            else if (size == 0)
                {
                    size = device->size() - at;
                }
            if (size < headerSize)
                {
                    break;
                }
            if (u32(header, 4) == fourcc("moov") && size < (qint64(1) << 30))
                {
                    device->seek(at + headerSize);
                    moov = device->read(size - headerSize);
                    break;
                }
            at += size;
        }
    if (moov.isEmpty())
        {
            return fail(QCoreApplication::translate("GpmfImporter", "Not a video file this program can read data from."));
        }
    const QVector<Sample> samples = dataSamples(moov);
    if (samples.isEmpty())
        {
            return fail(QCoreApplication::translate("GpmfImporter",
                        "The video holds no data of its own. Only GoPro cameras write positions into their videos."));
        }
    QVector<Payload> payloads;
    for (const Sample &sample : samples)
        {
            if (sample.size <= 0 || sample.size > (1 << 24) || !device->seek(sample.offset))
                {
                    continue;
                }
            payloads.append({sample.start, sample.duration, device->read(sample.size)});
        }
    const TelemetrySessionPtr session = fromPayloads(payloads);
    if (!session)
        {
            return fail(QCoreApplication::translate("GpmfImporter",
                        "The camera had no satellite fix while this video was recorded: there are no positions in it."));
        }
    return session;
}
