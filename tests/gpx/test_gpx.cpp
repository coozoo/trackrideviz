#include <QtTest>

#include <QBuffer>

#include "telemetry/channelids.h"
#include "telemetry/gpximporter.h"

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

QTEST_APPLESS_MAIN(TestGpx)
#include "test_gpx.moc"
