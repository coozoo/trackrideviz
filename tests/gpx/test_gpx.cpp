#include <QtTest>

#include <QBuffer>
#include <QtEndian>

#include "telemetry/channelids.h"
#include "telemetry/gpmfimporter.h"
#include "telemetry/gpximporter.h"
#include "telemetry/gpsproblems.h"
#include "telemetry/nmeaimporter.h"
#include "telemetry/vboimporter.h"
#include "telemetry/tcximporter.h"
#include "telemetry/telemetryimporter.h"
#include "telemetry/telemetrywriter.h"
#include "video/videojoin.h"

#include <QTemporaryDir>

class TestGpx : public QObject
{
    Q_OBJECT

private slots:
    void basicTrackWithExtensions();
    void savedDataReadsBack();
    void gpsProblemsAreFoundAndFixed();
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
    void cameraMotionSensors();
    void nmeaLog();
    void vboFile();
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

// Optional: RTV_GPX_SAMPLE=/path/to/file runs the importer over a real recording: a .gpx, or any
// other file the program reads (a camera's .mp4 among them).
void TestGpx::sampleFile()
{
    const QString path = qEnvironmentVariable("RTV_GPX_SAMPLE");
    if (path.isEmpty())
        {
            QSKIP("RTV_GPX_SAMPLE is not set");
        }
    QString error;
    const TelemetrySessionPtr session = TelemetryImporter::load(path, &error);
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

void TestGpx::cameraMotionSensors()
{
    // a second of positions with 400 readings of each sensor beside them, scaled as cameras do
    QByteArray accel;
    QByteArray gyro;
    auto be16 = [](qint16 v)
    {
        QByteArray b(2, '\0');
        qToBigEndian<qint16>(v, b.data());
        return b;
    };
    for (int i = 0; i < 400; ++i)
        {
            accel += be16(4180) + be16(i < 200 ? 0 : 418) + be16(-209);    // 10, 0 or 1, -0.5 m/s2
            gyro += be16(0) + be16(i < 200 ? 0 : 3755) + be16(0);           // 0 or 3.5 rad/s
        }
    const QByteArray sensors = nested("DEVC", nested("STRM", record("SCAL", 's', 2, 1, be16(418))
                                                     + record("ACCL", 's', 6, 400, accel))
                                      + nested("STRM", record("SCAL", 's', 2, 1, be16(1073))
                                               + record("GYRO", 's', 6, 400, gyro)));
    const TelemetrySessionPtr session
        = GpmfImporter::fromPayloads({{0.0, 1.0, positions(0, 2, 3, "260625100000.000") + sensors}});
    QVERIFY(session);
    const Channel *x = session->channel(ChannelId::AccelX);
    const Channel *y = session->channel(ChannelId::AccelY);
    QVERIFY(x && y && session->channel(ChannelId::AccelZ) && session->channel(ChannelId::GyroY));
    // twenty to the second, each the mean of its readings
    QCOMPARE(x->count(), 20);
    QVERIFY(std::fabs(x->valueAtIndex(0) - 10.0) < 1e-9);
    QVERIFY(std::fabs(y->valueAtIndex(2)) < 1e-9);
    QVERIFY(std::fabs(y->valueAtIndex(15) - 1.0) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::AccelZ)->valueAtIndex(0) + 0.5) < 1e-9);
    // degrees a second
    QVERIFY(std::fabs(session->channel(ChannelId::GyroY)->valueAtIndex(15) - 3.4995 * 57.29577951308232) < 0.01);
}

namespace
{
// "$body*hh" with the checksum the receiver would write
QByteArray nmea(const QByteArray &body)
{
    quint8 sum = 0;
    for (const char c : body)
        {
            sum ^= quint8(c);
        }
    return "$" + body + "*" + QByteArray::number(sum, 16).rightJustified(2, '0').toUpper() + "\r\n";
}
}

void TestGpx::nmeaLog()
{
    // a minute before midnight to a second after, once a second; one moment without a fix,
    // one line damaged on the way
    QByteArray log = "Logger v1.2 started\r\n";
    for (int i = 0; i < 4; ++i)
        {
            const int second = 58 + i;   // 23:59:58 .. 00:00:01
            const QByteArray time = second < 60 ? QByteArray("2359") + QByteArray::number(second) + ".00"
                                    : QByteArray("0000") + QByteArray::number(second - 60).rightJustified(2, '0') + ".00";
            const QByteArray date = second < 60 ? "250626" : "260626";
            const QByteArray minutes = QByteArray::number(30.0 + i * 0.01, 'f', 4);
            log += nmea("GPRMC," + time + "," + (i == 2 ? "V" : "A") + ",49" + minutes + ",N,02800.0000,W,"
                        + QByteArray::number(10.0 + i) + ",90.5," + date + ",,,A");
            log += nmea("GNGGA," + time + ",49" + minutes + ",N,02800.0000,W,1,08,0.9," + QByteArray::number(100 + i)
                        + ".0,M,40.0,M,,");
        }
    log += "$GPRMC,000002.00,A,4930.0500,N,02800.0000,W,14,90.5,260626,,,A*00\r\n";
    QBuffer buffer(&log);
    buffer.open(QIODevice::ReadOnly);
    QString error;
    const TelemetrySessionPtr session = NmeaImporter::read(&buffer, &error);
    QVERIFY2(session, qPrintable(error));
    const Channel *lat = session->channel(ChannelId::PositionLat);
    // without the moment without a fix and the damaged line
    QCOMPARE(lat->count(), 3);
    QCOMPARE(lat->timeAt(2), 3.0);    // on over midnight
    QVERIFY(std::fabs(lat->valueAtIndex(0) - 49.5) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::PositionLon)->valueAtIndex(0) + 28.0) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::Speed)->valueAtIndex(0) - 10.0 * 1852.0 / 3600.0) < 1e-9);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAt(1.0), 101.0);
    QCOMPARE(session->channel(ChannelId::Heading)->valueAt(0.0), 90.5);
    QCOMPARE(session->startUtc(), QDateTime(QDate(2026, 6, 25), QTime(23, 59, 58), QTimeZone::utc()));
    QCOMPARE(session->warnings().size(), 1);

    // .txt and .log are read as NMEA only when they are
    QTemporaryDir dir;
    QFile file(dir.filePath("ride.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(log);
    file.close();
    QVERIFY(NmeaImporter::looksLikeNmea(file.fileName()));
    QVERIFY(TelemetryImporter::loadRecorded(file.fileName()));
    QFile csv(dir.filePath("other.txt"));
    QVERIFY(csv.open(QIODevice::WriteOnly));
    csv.write("time,speed\n0,1\n");
    csv.close();
    QVERIFY(!NmeaImporter::looksLikeNmea(csv.fileName()));
}

void TestGpx::vboFile()
{
    QByteArray vbo =
        "File created on 31/07/2026 at 09:34:57\r\n"
        "\r\n"
        "[header]\r\nsatellites\r\ntime\r\nlatitude\r\nlongitude\r\nvelocity kmh\r\nheading\r\nheight\r\nrpm\r\n"
        "\r\n[channel units]\r\n\r\n\r\n\r\n\r\nkmh\r\ndeg\r\nm\r\nrpm\r\n"
        "\r\n[comments]\r\nLog rate 10 Hz\r\n"
        "\r\n[column names]\r\nsats time lat long velocity heading height rpm\r\n"
        "\r\n[data]\r\n"
        "008 093457.40 +03119.09616 +00058.49580 036.000 140.47 +00106.14 4500\r\n"
        "008 093457.50 +03119.09700 +00058.49500 037.800 140.50 +00106.20 4600\r\n"
        "000 093457.60 +00000.00000 +00000.00000 000.000 000.00 +00000.00 4700\r\n"
        "008 093457\r\n";
    QBuffer buffer(&vbo);
    buffer.open(QIODevice::ReadOnly);
    QString error;
    const TelemetrySessionPtr session = VboImporter::read(&buffer, &error);
    QVERIFY2(session, qPrintable(error));
    const Channel *lat = session->channel(ChannelId::PositionLat);
    // the row without a fix has no position
    QCOMPARE(lat->count(), 2);
    QVERIFY(std::fabs(lat->valueAtIndex(0) - 3119.09616 / 60.0) < 1e-9);
    // minutes, positive to the west
    QVERIFY(std::fabs(session->channel(ChannelId::PositionLon)->valueAtIndex(0) + 58.4958 / 60.0) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::Speed)->valueAtIndex(0) - 10.0) < 1e-9);
    QVERIFY(std::fabs(session->channel(ChannelId::Speed)->timeAt(1) - 0.1) < 1e-9);
    QCOMPARE(session->channel(ChannelId::Elevation)->valueAtIndex(1), 106.2);
    // a column of its own, with its unit
    QVERIFY(session->channel("rpm"));
    QCOMPARE(session->channel("rpm")->count(), 3);
    QCOMPARE(session->channel("rpm")->unit(), QString("rpm"));
    QCOMPARE(session->startUtc(), QDateTime(QDate(2026, 7, 31), QTime(9, 34, 57, 400), QTimeZone::utc()));
    QCOMPARE(session->warnings().size(), 1);

    QByteArray junk("[data]\r\n1 2 3\r\n");
    QBuffer none(&junk);
    none.open(QIODevice::ReadOnly);
    QVERIFY(!VboImporter::read(&none, &error));
    QVERIFY(TelemetryImporter::suffixes().contains("vbo"));
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

// What is saved is what is read again: GPX with its extensions, CSV with every channel.
void TestGpx::savedDataReadsBack()
{
    auto session = std::make_shared<TelemetrySession>();
    session->setStartUtc(QDateTime(QDate(2025, 7, 15), QTime(7, 47, 10), QTimeZone::UTC));
    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &ele = session->channelForWriting(ChannelId::Elevation, "m");
    Channel &hr = session->channelForWriting(ChannelId::HeartRate, "bpm");
    Channel &power = session->channelForWriting(ChannelId::Power, "W");
    Channel &own = session->channelForWriting("dev.Core Temperature", "");
    for (int i = 0; i < 10; ++i)
        {
            lat.append(i, 50.4501 + i * 1e-4);
            lon.append(i, 30.5234 + i * 1e-4);
            ele.append(i, 150.0 + i * 0.5);
            hr.append(i, 120 + i);
            power.append(i, 200 + 10 * i);
        }
    // a channel of its own rate, between the others' samples
    own.append(0.5, 37.1);
    own.append(4.25, 37.4);

    QTemporaryDir dir;
    QString error;
    const QString gpxPath = dir.filePath("saved.gpx");
    QVERIFY2(TelemetryWriter::save(*session, gpxPath, &error), qPrintable(error));
    const TelemetrySessionPtr gpx = TelemetryImporter::loadRecorded(gpxPath, &error);
    QVERIFY2(gpx, qPrintable(error));
    QCOMPARE(gpx->startUtc(), session->startUtc());
    QCOMPARE(gpx->channel(ChannelId::PositionLat)->count(), 10);
    QCOMPARE(gpx->channel(ChannelId::PositionLat)->valueAt(3.0), 50.4504);
    QCOMPARE(gpx->channel(ChannelId::Elevation)->valueAt(9.0), 154.5);
    QCOMPARE(gpx->channel(ChannelId::HeartRate)->valueAt(5.0), 125.0);
    QCOMPARE(gpx->channel(ChannelId::Power)->valueAt(2.0), 220.0);

    const QString csvPath = dir.filePath("saved.csv");
    QVERIFY2(TelemetryWriter::save(*session, csvPath, &error), qPrintable(error));
    const TelemetrySessionPtr csv = TelemetryImporter::loadRecorded(csvPath, &error);
    QVERIFY2(csv, qPrintable(error));
    QCOMPARE(csv->startUtc(), session->startUtc());
    for (const QString &id : session->channelIds())
        {
            const Channel *before = session->channel(id);
            const Channel *after = csv->channel(id);
            QVERIFY2(after, qPrintable(id));
            QCOMPARE(after->count(), before->count());
            for (int i = 0; i < before->count(); ++i)
                {
                    QCOMPARE(after->timeAt(i), before->timeAt(i));
                    QCOMPARE(after->valueAtIndex(i), before->valueAtIndex(i));
                }
        }

    // nothing to put on a track
    auto noPositions = std::make_shared<TelemetrySession>();
    noPositions->channelForWriting(ChannelId::HeartRate, "bpm").append(0.0, 100.0);
    QVERIFY(!TelemetryWriter::save(*noPositions, dir.filePath("none.gpx"), &error));
    QVERIFY(!error.isEmpty());
}

// A made-up ride at 10 m/s north, a position a second, with every kind of trouble.
void TestGpx::gpsProblemsAreFoundAndFixed()
{
    const double north = 10.0 / 111195.0;   // 10 m in degrees of latitude
    auto session = std::make_shared<TelemetrySession>();
    Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
    Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
    Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
    for (int t = 0; t <= 400; ++t)
        {
            speed.append(t, 10.0);
            if (t > 100 && t < 130)
                {
                    continue;   // a tunnel
                }
            if (t > 300 && t < 340)
                {
                    continue;   // the recording paused
                }
            double latitude = 50.0 + t * north;
            double longitude = 30.0;
            if (t == 50)
                {
                    longitude += 0.007;   // about 500 m off to the east, and back
                }
            if (t > 200 && t <= 210)
                {
                    latitude = 50.0 + 200 * north;   // the position stuck while the wheel turned
                }
            lat.append(t, latitude);
            lon.append(t, longitude);
        }
    session->addPause({300.0, 340.0});

    const QVector<GpsProblems::Problem> found = GpsProblems::find(*session);
    QCOMPARE(found.size(), 3);
    QCOMPARE(found[0].kind, GpsProblems::Problem::Jump);
    QCOMPARE(found[0].start, 50.0);
    QCOMPARE(found[0].end, 50.0);
    QVERIFY2(found[0].meters > 450.0 && found[0].meters < 550.0, qPrintable(QString::number(found[0].meters)));
    QCOMPARE(found[1].kind, GpsProblems::Problem::Gap);
    QCOMPARE(found[1].start, 100.0);
    QCOMPARE(found[1].end, 130.0);
    QVERIFY(!found[1].fixable());
    QCOMPARE(found[2].kind, GpsProblems::Problem::Frozen);
    QCOMPARE(found[2].start, 201.0);
    QCOMPARE(found[2].end, 210.0);

    // fixed: only the gap is left, and the track goes straight on where the jump was
    QVector<DataEdit> edits;
    for (const GpsProblems::Problem &problem : found)
        {
            if (problem.fixable())
                {
                    edits.append(problem.fix());
                }
        }
    const TelemetrySessionPtr fixed = DataEdits::apply(session, edits);
    const QVector<GpsProblems::Problem> left = GpsProblems::find(*fixed, edits);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left[0].kind, GpsProblems::Problem::Gap);
    QCOMPARE(fixed->channel(ChannelId::PositionLon)->valueAt(50.0), 30.0);
    QVERIFY(std::fabs(fixed->channel(ChannelId::PositionLat)->valueAt(205.0) - (50.0 + 205 * north)) < 1e-9);

    // a speed worked out from the positions says nothing about them: nothing frozen
    auto derived = std::make_shared<TelemetrySession>(*session);
    derived->addDerivedChannel(ChannelId::Speed);
    QCOMPARE(GpsProblems::find(*derived).size(), 2);
}

QTEST_APPLESS_MAIN(TestGpx)
#include "test_gpx.moc"
