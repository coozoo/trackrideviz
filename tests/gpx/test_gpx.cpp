#include <QtTest>

#include <QBuffer>
#include <QtEndian>

#include "telemetry/channelids.h"
#include "telemetry/gpmfimporter.h"
#include "telemetry/gpximporter.h"
#include "telemetry/tcximporter.h"
#include "telemetry/telemetryimporter.h"
#include "video/videojoin.h"

#include <QTemporaryDir>

class TestGpx : public QObject
{
    Q_OBJECT

private slots:
    void basicTrackWithExtensions();
    void derivesSpeedAndDistance();
    void joinsSegmentsAndTracks();
    void utf8BomAndFractionalSeconds();
    void noTimestampsIsAnError();
    void noPointsIsAnError();
    void garbageIsAnError();
    void truncatedFileKeepsPoints();
    void backwardTimeIsSkipped();
    void sampleFile();
    void tcxTrackWithLaps();
    void tcxErrors();
    void cameraPositions();
    void cameraVideoFile();
    void cameraChapters();
    void cameraNestingIsLimited();
    void strayTimesAreLeftOut();

private:
    static TelemetrySessionPtr parse(const QByteArray &gpx, QString *error = nullptr);
};

TelemetrySessionPtr TestGpx::parse(const QByteArray &gpx, QString *error)
{
    QByteArray copy = gpx;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    return GpxImporter::read(&buffer, error);
}

void TestGpx::basicTrackWithExtensions()
{
    const QByteArray gpx = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx creator="Unit Test" version="1.1" xmlns="http://www.topografix.com/GPX/1/1"
     xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1">
 <trk><trkseg>
  <trkpt lat="49.0" lon="28.0"><ele>100.0</ele><time>2026-06-25T10:00:00Z</time>
   <extensions><power>200</power><gpxtpx:TrackPointExtension>
     <gpxtpx:hr>120</gpxtpx:hr><gpxtpx:cad>80</gpxtpx:cad><gpxtpx:atemp>21.5</gpxtpx:atemp>
   </gpxtpx:TrackPointExtension></extensions>
  </trkpt>
  <trkpt lat="49.001" lon="28.0"><ele>110.0</ele><time>2026-06-25T10:00:10Z</time>
   <extensions><power>300</power><gpxtpx:TrackPointExtension>
     <gpxtpx:hr>140</gpxtpx:hr><gpxtpx:cad>90</gpxtpx:cad>
   </gpxtpx:TrackPointExtension></extensions>
  </trkpt>
 </trkseg></trk>
</gpx>)";

    QString error;
    const TelemetrySessionPtr session = parse(gpx, &error);
    QVERIFY2(session, qPrintable(error));
    QCOMPARE(session->startUtc(), QDateTime::fromString("2026-06-25T10:00:00Z", Qt::ISODate));
    QCOMPARE(session->duration(), 10.0);
    QVERIFY(session->sourceInfo().contains("Unit Test"));

    QCOMPARE(session->channel(ChannelId::PositionLat)->valueAt(5.0), 49.0005);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAt(5.0), 105.0);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAt(5.0), 130.0);
    QCOMPARE(session->channel(ChannelId::Cadence)->valueAt(10.0), 90.0);
    QCOMPARE(session->channel(ChannelId::Power)->valueAt(0.0), 200.0);
    // only the first point has a temperature
    QCOMPARE(session->channel(ChannelId::Temperature)->count(), 1);
    // nothing in the file gives a heading
    QVERIFY(!session->hasChannel(ChannelId::Heading));
}

void TestGpx::derivesSpeedAndDistance()
{
    // 0.001 degree of latitude is 111.2 m; covered in 10 s
    const QByteArray gpx = R"(<gpx><trk><trkseg>
  <trkpt lat="49.000" lon="28.0"><time>2026-06-25T10:00:00Z</time></trkpt>
  <trkpt lat="49.001" lon="28.0"><time>2026-06-25T10:00:10Z</time></trkpt>
  <trkpt lat="49.002" lon="28.0"><time>2026-06-25T10:00:20Z</time></trkpt>
 </trkseg></trk></gpx>)";

    const TelemetrySessionPtr session = parse(gpx);
    QVERIFY(session);
    const Channel *distance = session->channel(ChannelId::Distance);
    const Channel *speed = session->channel(ChannelId::Speed);
    QVERIFY(distance && speed);
    QCOMPARE(distance->valueAt(0.0), 0.0);
    QVERIFY(qAbs(distance->valueAt(20.0) - 222.4) < 0.5);
    QVERIFY(qAbs(speed->valueAt(15.0) - 11.12) < 0.05);
}

void TestGpx::joinsSegmentsAndTracks()
{
    const QByteArray gpx = R"(<gpx>
 <trk><trkseg>
  <trkpt lat="49.0" lon="28.0"><time>2026-06-25T10:00:00Z</time></trkpt>
 </trkseg><trkseg>
  <trkpt lat="49.1" lon="28.0"><time>2026-06-25T10:00:05Z</time></trkpt>
 </trkseg></trk>
 <trk><trkseg>
  <trkpt lat="49.2" lon="28.0"><time>2026-06-25T10:00:09Z</time></trkpt>
 </trkseg></trk>
</gpx>)";

    const TelemetrySessionPtr session = parse(gpx);
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::PositionLat)->count(), 3);
    QCOMPARE(session->duration(), 9.0);
}

void TestGpx::utf8BomAndFractionalSeconds()
{
    const QByteArray gpx = QByteArray("\xEF\xBB\xBF") + R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx creator="IGPSPORT"><trk><trkseg>
  <trkpt lat="49.0" lon="28.0"><time>2026-06-25T19:47:10.000Z</time></trkpt>
  <trkpt lat="49.1" lon="28.0"><time>2026-06-25T19:47:10.250Z</time></trkpt>
 </trkseg></trk></gpx>)";

    QString error;
    const TelemetrySessionPtr session = parse(gpx, &error);
    QVERIFY2(session, qPrintable(error));
    QCOMPARE(session->duration(), 0.25);
}

void TestGpx::noTimestampsIsAnError()
{
    const QByteArray gpx = R"(<gpx><trk><trkseg>
  <trkpt lat="49.0" lon="28.0"/><trkpt lat="49.1" lon="28.0"/>
 </trkseg></trk></gpx>)";

    QString error;
    QVERIFY(!parse(gpx, &error));
    QVERIFY(error.contains("timestamps"));
}

void TestGpx::noPointsIsAnError()
{
    QString error;
    QVERIFY(!parse("<gpx><wpt lat=\"1\" lon=\"2\"/></gpx>", &error));
    QVERIFY(!error.isEmpty());
}

void TestGpx::garbageIsAnError()
{
    QString error;
    QVERIFY(!parse("this is not xml at all", &error));
    QVERIFY(!error.isEmpty());
}

void TestGpx::truncatedFileKeepsPoints()
{
    const QByteArray gpx = R"(<gpx><trk><trkseg>
  <trkpt lat="49.0" lon="28.0"><time>2026-06-25T10:00:00Z</time></trkpt>
  <trkpt lat="49.1" lon="28.0"><time>2026-06-25T10:00:05Z</time></trkpt>
  <trkpt lat="49.2" lon="28.0"><time>2026-06-25T10:0)";

    const TelemetrySessionPtr session = parse(gpx);
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::PositionLat)->count(), 2);
    QCOMPARE(session->warnings().size(), 1);
}

void TestGpx::backwardTimeIsSkipped()
{
    const QByteArray gpx = R"(<gpx><trk><trkseg>
  <trkpt lat="49.0" lon="28.0"><time>2026-06-25T10:00:00Z</time></trkpt>
  <trkpt lat="49.1" lon="28.0"><time>2026-06-25T10:00:05Z</time></trkpt>
  <trkpt lat="55.0" lon="28.0"><time>2026-06-25T10:00:05Z</time></trkpt>
  <trkpt lat="56.0" lon="28.0"><time>2026-06-25T10:00:01Z</time></trkpt>
  <trkpt lat="49.2" lon="28.0"><time>2026-06-25T10:00:10Z</time></trkpt>
 </trkseg></trk></gpx>)";

    const TelemetrySessionPtr session = parse(gpx);
    QVERIFY(session);
    QCOMPARE(session->channel(ChannelId::PositionLat)->count(), 3);
    // the skipped points must not leak into the distance either
    QVERIFY(session->channel(ChannelId::Distance)->valueAt(10.0) < 30000.0);
    QCOMPARE(session->warnings().size(), 1);
}

// Optional: RTV_GPX_SAMPLE=/path/to/file.gpx runs the importer over a real recording.
void TestGpx::sampleFile()
{
    const QString path = qEnvironmentVariable("RTV_GPX_SAMPLE");
    if (path.isEmpty())
        {
            QSKIP("RTV_GPX_SAMPLE is not set");
        }
    QString error;
    const TelemetrySessionPtr session = GpxImporter::load(path, &error);
    QVERIFY2(session, qPrintable(error));
    qInfo() << session->sourceInfo() << session->startUtc() << "duration" << session->duration()
            << "channels" << session->channelIds() << "warnings" << session->warnings();
    for (const QString &id : session->channelIds())
        {
            const Channel *c = session->channel(id);
            qInfo() << id << c->count() << "samples, mid value" << c->valueAt(session->duration() / 2);
        }
}

void TestGpx::tcxTrackWithLaps()
{
    QByteArray tcx = R"(<?xml version="1.0" encoding="UTF-8"?>
<TrainingCenterDatabase xmlns="http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2"
    xmlns:ns3="http://www.garmin.com/xmlschemas/ActivityExtension/v2">
 <Activities><Activity Sport="Biking"><Id>2026-06-25T10:00:00Z</Id>
  <Lap StartTime="2026-06-25T10:00:00Z"><TotalTimeSeconds>10</TotalTimeSeconds><Track>
   <Trackpoint><Time>2026-06-25T10:00:00Z</Time>
    <Position><LatitudeDegrees>49.0</LatitudeDegrees><LongitudeDegrees>28.0</LongitudeDegrees></Position>
    <AltitudeMeters>100.5</AltitudeMeters><DistanceMeters>0</DistanceMeters>
    <HeartRateBpm><Value>120</Value></HeartRateBpm><Cadence>80</Cadence>
    <Extensions><ns3:TPX><ns3:Speed>5.5</ns3:Speed><ns3:Watts>210</ns3:Watts></ns3:TPX></Extensions>
   </Trackpoint>
   <Trackpoint><Time>2026-06-25T10:00:05.500Z</Time>
    <AltitudeMeters>101</AltitudeMeters><DistanceMeters>30</DistanceMeters>
    <HeartRateBpm><Value>125</Value></HeartRateBpm>
   </Trackpoint>
  </Track></Lap>
  <Lap StartTime="2026-06-25T10:00:10Z"><TotalTimeSeconds>8</TotalTimeSeconds><Track>
   <Trackpoint><Time>2026-06-25T10:00:18Z</Time>
    <Position><LatitudeDegrees>49.001</LatitudeDegrees><LongitudeDegrees>28.0</LongitudeDegrees></Position>
    <DistanceMeters>111</DistanceMeters>
   </Trackpoint>
  </Track></Lap>
 </Activity></Activities>
</TrainingCenterDatabase>)";
    QBuffer buffer(&tcx);
    buffer.open(QIODevice::ReadOnly);
    QString error;
    const TelemetrySessionPtr session = TcxImporter::read(&buffer, &error);
    QVERIFY2(session, qPrintable(error));
    QCOMPARE(session->sourceInfo(), QString("TCX, Biking"));
    QCOMPARE(session->startUtc(), QDateTime(QDate(2026, 6, 25), QTime(10, 0), QTimeZone::utc()));
    // a point without a position still gives its other values
    QCOMPARE(session->channel(ChannelId::PositionLat)->count(), 2);
    QCOMPARE(session->channel(ChannelId::Distance)->count(), 3);
    QCOMPARE(session->channel(ChannelId::Distance)->timeAt(1), 5.5);
    QCOMPARE(session->channel(ChannelId::HeartRate)->valueAtIndex(1), 125.0);
    QCOMPARE(session->channel(ChannelId::Cadence)->valueAtIndex(0), 80.0);
    QCOMPARE(session->channel(ChannelId::Speed)->valueAtIndex(0), 5.5);
    QCOMPARE(session->channel(ChannelId::Power)->valueAtIndex(0), 210.0);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAtIndex(0), 100.5);
    QCOMPARE(session->laps().size(), 2);
    QCOMPARE(session->laps().at(1).startTime, 10.0);
    QCOMPARE(session->laps().at(1).endTime, 18.0);
}

void TestGpx::tcxErrors()
{
    QString error;
    QByteArray gpx = "<gpx><trk><trkseg><trkpt lat='1' lon='2'><time>2026-06-25T10:00:00Z</time></trkpt></trkseg></trk></gpx>";
    QBuffer wrong(&gpx);
    wrong.open(QIODevice::ReadOnly);
    QVERIFY(!TcxImporter::read(&wrong, &error));
    QVERIFY(!error.isEmpty());
    QByteArray empty = "<TrainingCenterDatabase><Activities/></TrainingCenterDatabase>";
    QBuffer none(&empty);
    none.open(QIODevice::ReadOnly);
    QVERIFY(!TcxImporter::read(&none, &error));
}

namespace
{
QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    qToBigEndian(v, b.data());
    return b;
}

// One record of the camera's data: key, type, size of a value, how many, the values, padding.
QByteArray record(const char *key, char type, int size, int repeat, const QByteArray &body)
{
    QByteArray r(key, 4);
    r.append(type).append(char(size)).append(char(repeat >> 8)).append(char(repeat & 0xff)).append(body);
    while (r.size() % 4)
        {
            r.append('\0');
        }
    return r;
}

QByteArray nested(const char *key, const QByteArray &body)
{
    return record(key, '\0', 1, int(body.size()), body);
}

// Positions north of 49 N, 28 E: 0.0001 degrees and 1.5 m/s more with every sample.
QByteArray positions(int first, int count, int fix, const char *utc)
{
    QByteArray rows;
    for (int i = first; i < first + count; ++i)
        {
            rows += be32(490000000 + 1000 * i) + be32(280000000) + be32(200000 + 500 * i) + be32(5000 + 1500 * i) + be32(0);
        }
    const QByteArray scale = be32(10000000) + be32(10000000) + be32(1000) + be32(1000) + be32(100);
    return nested("DEVC", nested("STRM", record("GPSF", 'L', 4, 1, be32(fix)) + record("GPSU", 'U', 16, 1, QByteArray(utc, 16))
                                 + record("SCAL", 'l', 4, 5, scale) + record("GPS5", 'l', 20, count, rows)));
}

QByteArray box(const char *type, const QByteArray &body)
{
    return be32(quint32(body.size() + 8)) + QByteArray(type, 4) + body;
}
}

void TestGpx::cameraPositions()
{
    // a second without a fix, then two with two positions each
    const QVector<GpmfImporter::Payload> payloads
    {
        {0.0, 1.0, positions(0, 2, 0, "260625095959.000")},
        {1.0, 1.0, positions(2, 2, 3, "260625100001.000")},
        {2.0, 1.0, positions(4, 2, 3, "260625100002.000")},
    };
    const TelemetrySessionPtr session = GpmfImporter::fromPayloads(payloads);
    QVERIFY(session);
    const Channel *lat = session->channel(ChannelId::PositionLat);
    QCOMPARE(lat->count(), 4);
    QCOMPARE(lat->timeAt(0), 1.0);
    QCOMPARE(lat->timeAt(1), 1.5);
    QVERIFY(std::fabs(lat->valueAtIndex(0) - 49.0002) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::PositionLon)->valueAtIndex(0) - 28.0) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::Elevation)->valueAtIndex(3) - 202.5) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::Speed)->valueAtIndex(3) - 12.5) < 1e-9);
    // the clock when the video began: a second before the first fix
    QCOMPARE(session->startUtc(), QDateTime(QDate(2026, 6, 25), QTime(10, 0, 0), QTimeZone::utc()));
    QVERIFY(!GpmfImporter::fromPayloads({{0.0, 1.0, positions(0, 2, 0, "260625095959.000")}}));
}

void TestGpx::cameraVideoFile()
{
    // a video file with nothing but the data track: two samples of half a second, in one chunk
    const QByteArray first = positions(0, 1, 3, "260625100000.000");
    const QByteArray second = positions(1, 1, 3, "260625100000.500");
    const QByteArray ftyp = box("ftyp", QByteArray("mp41", 4) + be32(0));
    const QByteArray mdat = box("mdat", first + second);
    const quint32 dataAt = quint32(ftyp.size() + 8);
    const QByteArray stbl = box("stbl",
                                box("stsd", be32(0) + be32(1) + be32(16) + QByteArray("gpmd", 4) + be32(0) + be32(1))
                                + box("stts", be32(0) + be32(1) + be32(2) + be32(500))
                                + box("stsc", be32(0) + be32(1) + be32(1) + be32(2) + be32(1))
                                + box("stsz", be32(0) + be32(0) + be32(2) + be32(quint32(first.size())) + be32(quint32(second.size())))
                                + box("stco", be32(0) + be32(1) + be32(dataAt)));
    const QByteArray mdhd = box("mdhd", be32(0) + be32(0) + be32(0) + be32(1000) + be32(1000) + be32(0));
    QByteArray file = ftyp + mdat + box("moov", box("trak", box("mdia", mdhd + box("minf", stbl))));
    QBuffer buffer(&file);
    buffer.open(QIODevice::ReadOnly);
    QString error;
    const TelemetrySessionPtr session = GpmfImporter::read(&buffer, &error);
    QVERIFY2(session, qPrintable(error));
    const Channel *lat = session->channel(ChannelId::PositionLat);
    QCOMPARE(lat->count(), 2);
    QCOMPARE(lat->timeAt(1), 0.5);
    QVERIFY(std::fabs(lat->valueAtIndex(1) - 49.0001) < 1e-9);

    // a video without such a track, and something that is no video at all
    QByteArray plain = ftyp + box("moov", box("trak", box("mdia", mdhd)));
    QBuffer other(&plain);
    other.open(QIODevice::ReadOnly);
    QVERIFY(!GpmfImporter::read(&other, &error));
    QVERIFY(!error.isEmpty());
    QByteArray junk("not a video at all, not even close to one");
    QBuffer none(&junk);
    none.open(QIODevice::ReadOnly);
    QVERIFY(!GpmfImporter::read(&none, &error));
}

namespace
{
// A video file `seconds` long holding one data sample a second, `first` the first position.
QByteArray cameraFile(int first, int seconds, const char *utc)
{
    QVector<QByteArray> samples;
    for (int i = 0; i < seconds; ++i)
        {
            samples << positions(first + i, 1, 3, utc);
        }
    const QByteArray ftyp = box("ftyp", QByteArray("mp41", 4) + be32(0));
    QByteArray data;
    QByteArray sizes;
    for (const QByteArray &sample : std::as_const(samples))
        {
            data += sample;
            sizes += be32(quint32(sample.size()));
        }
    const quint32 dataAt = quint32(ftyp.size() + 8);
    const QByteArray stbl = box("stbl",
                                box("stsd", be32(0) + be32(1) + be32(16) + QByteArray("gpmd", 4) + be32(0) + be32(1))
                                + box("stts", be32(0) + be32(1) + be32(quint32(seconds)) + be32(1000))
                                + box("stsc", be32(0) + be32(1) + be32(1) + be32(quint32(seconds)) + be32(1))
                                + box("stsz", be32(0) + be32(0) + be32(quint32(seconds)) + sizes)
                                + box("stco", be32(0) + be32(1) + be32(dataAt)));
    const QByteArray mdhd = box("mdhd", be32(0) + be32(0) + be32(0) + be32(1000) + be32(quint32(seconds * 1000)) + be32(0));
    // the video's length, version 0: times, the scale, the length, then what does not matter here
    const QByteArray mvhd = box("mvhd", be32(0) + be32(0) + be32(0) + be32(1000) + be32(quint32(seconds * 1000))
                                + QByteArray(80, '\0'));
    return ftyp + box("mdat", data) + box("moov", mvhd + box("trak", box("mdia", mdhd + box("minf", stbl))));
}

void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}
}

void TestGpx::cameraChapters()
{
    QTemporaryDir dir;
    // chapters 1 and 2 of recording 0042, 4 and 3 s; a chapter 4 after a missing 3; another
    // recording, and the same number from the other codec
    write(dir.filePath("GX010042.MP4"), cameraFile(0, 4, "260625100000.000"));
    write(dir.filePath("GX020042.MP4"), cameraFile(10, 3, "260625100004.000"));
    write(dir.filePath("GX040042.MP4"), cameraFile(20, 3, "260625100010.000"));
    write(dir.filePath("GX010043.MP4"), cameraFile(30, 3, "260625110000.000"));
    write(dir.filePath("GH010042.MP4"), cameraFile(40, 3, "260625120000.000"));
    // a video joined from the chapters, without their data
    write(dir.filePath("GX010042_joined.MP4"), box("ftyp", QByteArray("mp41", 4) + be32(0)) + box("moov", QByteArray()));

    QCOMPARE(VideoJoin::chaptersFrom(dir.filePath("GX010042.MP4")),
             QStringList({dir.filePath("GX010042.MP4"), dir.filePath("GX020042.MP4")}));
    QCOMPARE(VideoJoin::chaptersFrom(dir.filePath("GX020042.MP4")), QStringList{dir.filePath("GX020042.MP4")});
    QCOMPARE(VideoJoin::chaptersFrom(dir.filePath("GX010042_joined.MP4")).size(), 2);
    QVERIFY(VideoJoin::chaptersFrom(dir.filePath("ride.mp4")).isEmpty());

    // from the first chapter: the whole recording, the second where the first ends
    QString error;
    const TelemetrySessionPtr whole = GpmfImporter::load(dir.filePath("GX010042.MP4"), &error);
    QVERIFY2(whole, qPrintable(error));
    const Channel *lat = whole->channel(ChannelId::PositionLat);
    QCOMPARE(lat->count(), 7);
    QCOMPARE(lat->timeAt(4), 4.0);
    QVERIFY(std::fabs(lat->valueAtIndex(4) - 49.0010) < 1e-9);
    QCOMPARE(whole->startUtc(), QDateTime(QDate(2026, 6, 25), QTime(10, 0, 0), QTimeZone::utc()));
    QVERIFY(whole->sourceInfo().contains("2"));
    // from the second: that one on its own clock
    const TelemetrySessionPtr second = GpmfImporter::load(dir.filePath("GX020042.MP4"), &error);
    QCOMPARE(second->channel(ChannelId::PositionLat)->count(), 3);
    QCOMPARE(second->channel(ChannelId::PositionLat)->timeAt(0), 0.0);
    // the joined video: from the chapters beside it
    const TelemetrySessionPtr joined = GpmfImporter::load(dir.filePath("GX010042_joined.MP4"), &error);
    QVERIFY2(joined, qPrintable(error));
    QCOMPARE(joined->channel(ChannelId::PositionLat)->count(), 7);

    // the older naming: GOPR0007, then GP010007
    write(dir.filePath("GOPR0007.MP4"), cameraFile(0, 2, "260625100000.000"));
    write(dir.filePath("GP010007.MP4"), cameraFile(2, 2, "260625100002.000"));
    QCOMPARE(VideoJoin::chaptersFrom(dir.filePath("GOPR0007.MP4")),
             QStringList({dir.filePath("GOPR0007.MP4"), dir.filePath("GP010007.MP4")}));
}

void TestGpx::cameraNestingIsLimited()
{
    // records inside records 30000 deep (240 KB): no camera writes that; read until the stack ran out
    const int depth = 30000;
    QByteArray data(8 * depth, '\0');
    for (int i = 0; i < depth; ++i)
        {
            char *at = data.data() + 8 * i;
            memcpy(at, "DEVC", 4);
            at[4] = '\0';   // nested
            at[5] = 4;      // 4 bytes ...
            qToBigEndian<quint16>(quint16(2 * (depth - 1 - i)), at + 6); // ... times the rest
        }
    QVERIFY(!GpmfImporter::fromPayloads({{0.0, 1.0, data}}));
}

void TestGpx::strayTimesAreLeftOut()
{
    // a point stamped 1970 (a clock not set yet) before an hour of riding, a point a second
    QByteArray gpx = R"(<?xml version="1.0"?><gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1"><trk><trkseg>
        <trkpt lat="49.0" lon="28.0"><time>1970-01-01T00:00:00Z</time></trkpt>)";
    for (int i = 0; i < 3600; ++i)
        {
            gpx += QString(R"(<trkpt lat="%1" lon="28.0"><time>%2</time></trkpt>)")
                   .arg(49.0 + i * 1e-5, 0, 'f', 6)
                   .arg(QDateTime(QDate(2026, 6, 25), QTime(10, 0), QTimeZone::utc()).addSecs(i).toString(Qt::ISODate)).toUtf8();
        }
    gpx += "</trkseg></trk></gpx>";
    QTemporaryDir dir;
    QFile file(dir.filePath("ride.gpx"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(gpx);
    file.close();
    const TelemetrySessionPtr session = TelemetryImporter::loadRecorded(file.fileName());
    QVERIFY(session);
    const Channel *lat = session->channel(ChannelId::PositionLat);
    QCOMPARE(lat->count(), 3600);
    QVERIFY(lat->endTime() - lat->startTime() < 3600.0);
    QVERIFY(session->warnings().join(' ').contains("away from the rest"));
    // worked out second by second without going through 56 years
    QVERIFY(TelemetryImporter::load(file.fileName()));
}

QTEST_APPLESS_MAIN(TestGpx)
#include "test_gpx.moc"
