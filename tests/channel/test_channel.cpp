#include <QtTest>

#include <QTemporaryDir>

#include <cmath>

#include "telemetry/channel.h"
#include "telemetry/channelids.h"
#include "telemetry/datasources.h"
#include "telemetry/telemetryimporter.h"
#include "telemetry/smoothing.h"
#include "telemetry/telemetrysession.h"

class TestChannel : public QObject
{
    Q_OBJECT

private slots:
    void smoothingEvensOutNoise()
    {
        // 10 up and down around 20, once a second; a pause of a minute; then a step up to 40
        Channel speed("speed", "m/s");
        for (int t = 0; t < 30; ++t)
            {
                speed.append(t, t % 2 ? 30.0 : 10.0);
            }
        for (int t = 90; t < 100; ++t)
            {
                speed.append(t, 40.0);
            }
        const Channel smooth = Smoothing::movingAverage(speed, 4.0);
        QCOMPARE(smooth.count(), speed.count());
        QVERIFY(std::fabs(smooth.valueAt(15.0) - 20.0) < 2.5);   // the swings mostly gone
        QCOMPARE(smooth.valueAt(95.0), 40.0);                     // nothing of before the pause
        QCOMPARE(smooth.valueAt(90.0), 40.0);
        // nothing to do
        QCOMPARE(Smoothing::movingAverage(speed, 0.0).valueAt(15.0), speed.valueAt(15.0));
        Channel lap("lap", QString(), Channel::Step);
        lap.append(0, 1);
        lap.append(10, 2);
        lap.append(20, 3);
        QCOMPARE(Smoothing::movingAverage(lap, 30.0).valueAt(12.0), 2.0);
    }

    void emptyChannel();
    void linearInterpolation();
    void stepInterpolation();
    void outOfRangeClampsAndReportsNotOk();
    void rejectsNonIncreasingTime();
    void irregularSampleRate();
    void steadyMaxLeavesOutSpikes();
    void sessionChannelReferencesStayValid();
    void addedDataFollowsTheClocks();
    void addedDataIsMerged();
    void addedFilesAreLoaded();
};

void TestChannel::emptyChannel()
{
    Channel c("speed", "m/s");
    bool ok = true;
    QCOMPARE(c.valueAt(1.0, &ok), 0.0);
    QVERIFY(!ok);
    QCOMPARE(c.count(), 0);
}

void TestChannel::linearInterpolation()
{
    Channel c("speed", "m/s");
    QVERIFY(c.append(0.0, 10.0));
    QVERIFY(c.append(1.0, 20.0));
    QVERIFY(c.append(2.0, 0.0));

    bool ok = false;
    QCOMPARE(c.valueAt(0.0, &ok), 10.0);
    QVERIFY(ok);
    QCOMPARE(c.valueAt(0.25, &ok), 12.5);
    QVERIFY(ok);
    QCOMPARE(c.valueAt(1.0, &ok), 20.0);
    QCOMPARE(c.valueAt(1.5, &ok), 10.0);
    QCOMPARE(c.valueAt(2.0, &ok), 0.0);
    QVERIFY(ok);
}

void TestChannel::stepInterpolation()
{
    Channel c("lapNumber", "", Channel::Step);
    c.append(0.0, 1.0);
    c.append(60.0, 2.0);
    c.append(118.5, 3.0);

    QCOMPARE(c.valueAt(59.999), 1.0);
    QCOMPARE(c.valueAt(60.0), 2.0);
    QCOMPARE(c.valueAt(100.0), 2.0);
    QCOMPARE(c.valueAt(118.5), 3.0);
}

void TestChannel::outOfRangeClampsAndReportsNotOk()
{
    Channel c("heartRate", "bpm");
    c.append(5.0, 120.0);
    c.append(6.0, 130.0);

    bool ok = true;
    QCOMPARE(c.valueAt(0.0, &ok), 120.0);
    QVERIFY(!ok);
    ok = true;
    QCOMPARE(c.valueAt(99.0, &ok), 130.0);
    QVERIFY(!ok);
    // no time at all: no value, and nothing read past the end
    ok = true;
    QCOMPARE(c.valueAt(std::nan(""), &ok), 0.0);
    QVERIFY(!ok);
    QCOMPARE(c.startTime(), 5.0);
    QCOMPARE(c.endTime(), 6.0);
}

void TestChannel::rejectsNonIncreasingTime()
{
    Channel c("speed", "m/s");
    QVERIFY(c.append(1.0, 1.0));
    QVERIFY(!c.append(1.0, 2.0));
    QVERIFY(!c.append(0.5, 3.0));
    QVERIFY(c.append(1.1, 4.0));
    QCOMPARE(c.count(), 2);
    QCOMPARE(c.valueAt(1.0), 1.0);
}

void TestChannel::irregularSampleRate()
{
    Channel c("altitude", "m");
    c.append(0.0, 100.0);
    c.append(0.1, 101.0);
    c.append(10.0, 200.0);

    QCOMPARE(c.valueAt(0.05), 100.5);
    QCOMPARE(c.valueAt(5.05), 150.5);
    QCOMPARE(c.minValue(), 100.0);
    QCOMPARE(c.maxValue(), 200.0);
    QCOMPARE(Channel().minValue(), 0.0);
    QCOMPARE(Channel().maxValue(), 0.0);
}

void TestChannel::steadyMaxLeavesOutSpikes()
{
    // 10 times a second: up to 30, a reading of 90 and a third of a second at 70
    Channel speed("speed", "m/s");
    for (int i = 0; i <= 600; ++i)
        {
            const double t = i / 10.0;
            const double value = i == 100 ? 90.0 : (i >= 200 && i < 203) ? 70.0 : 30.0 * std::sin(t / 20.0);
            speed.append(t, value);
        }
    QCOMPARE(speed.maxValue(), 90.0);
    const double steady = speed.steadyMax(1.0);
    QVERIFY2(steady > 29.0 && steady < 30.0, qPrintable(QString::number(steady)));
    // once a second: a lone reading is not held either
    Channel sparse("speed", "m/s");
    for (int t = 0; t < 10; ++t)
        {
            sparse.append(t, t == 5 ? 80.0 : 20.0);
        }
    QCOMPARE(sparse.steadyMax(1.0), 20.0);
    // too short to say
    Channel one("speed", "m/s");
    one.append(0.0, 12.0);
    QCOMPARE(one.steadyMax(1.0), 12.0);
}

// Importers keep references to several channels while creating more.
void TestChannel::sessionChannelReferencesStayValid()
{
    TelemetrySession session;
    Channel &first = session.channelForWriting("first", "m");
    first.append(0.0, 1.0);
    for (int i = 0; i < 200; ++i)
        {
            session.channelForWriting(QString("extra%1").arg(i), QString()).append(0.0, i);
        }
    QVERIFY(first.append(1.0, 2.0));
    QCOMPARE(&first, session.channel("first"));
    QCOMPARE(session.channel("first")->count(), 2);
    QCOMPARE(session.channelIds().size(), 201);
    QCOMPARE(session.channelIds().first(), QString("first"));
}

namespace
{
// a recording of `seconds` samples a second apart, starting at `start` (UTC)
TelemetrySessionPtr recording(const QDateTime &start, const QString &id, double value, int seconds)
{
    auto session = std::make_shared<TelemetrySession>();
    session->setStartUtc(start);
    Channel &channel = session->channelForWriting(id, QString());
    for (int t = 0; t < seconds; ++t)
        {
            channel.append(t, value + t);
        }
    return session;
}

const QDateTime kNoon(QDate(2025, 6, 1), QTime(12, 0), QTimeZone::utc());
}

void TestChannel::addedDataFollowsTheClocks()
{
    const TelemetrySessionPtr main = recording(kNoon, ChannelId::Speed, 5.0, 100);
    double shift = 0.0;
    QVERIFY(DataSources::clockShift(*main, *recording(kNoon.addSecs(30), ChannelId::HeartRate, 120.0, 10), &shift));
    QCOMPARE(shift, 30.0);
    QVERIFY(DataSources::clockShift(*main, *recording(kNoon.addSecs(-5), ChannelId::HeartRate, 120.0, 10), &shift));
    QCOMPARE(shift, -5.0);
    // no time of day: nothing to go by
    QVERIFY(!DataSources::clockShift(*main, *recording(QDateTime(), ChannelId::HeartRate, 120.0, 10), &shift));

    auto added = std::make_shared<TelemetrySession>();
    added->channelForWriting(ChannelId::Speed, "m/s").append(0, 1);
    added->channelForWriting(ChannelId::HeartRate, "bpm").append(0, 1);
    QCOMPARE(DataSources::missingChannels(*main, *added), QStringList{ChannelId::HeartRate});
}

void TestChannel::addedDataIsMerged()
{
    const TelemetrySessionPtr main = recording(kNoon, ChannelId::Speed, 5.0, 100);
    auto strap = std::make_shared<TelemetrySession>();
    Channel &hr = strap->channelForWriting(ChannelId::HeartRate, "bpm");
    Channel &speed = strap->channelForWriting(ChannelId::Speed, "m/s");
    for (int t = 0; t < 20; ++t)
        {
            hr.append(t, 100 + t);
            speed.append(t, 50.0);
        }
    // started 5 s before the main data: its first 5 s fall before the start
    DataSource source{"/rides/strap.fit", -5.0, false, {ChannelId::HeartRate}};
    const TelemetrySessionPtr merged = DataSources::merged(main, {source}, {strap});
    const Channel *mergedHr = merged->channel(ChannelId::HeartRate);
    QVERIFY(mergedHr);
    QCOMPARE(mergedHr->count(), 15);
    QCOMPARE(mergedHr->startTime(), 0.0);
    QCOMPARE(mergedHr->valueAt(0.0), 105.0);
    // the main data's own stays, as it is not taken from the file
    QCOMPARE(merged->channel(ChannelId::Speed)->valueAt(10.0), 15.0);
    QCOMPARE(merged->startUtc(), kNoon);
    QVERIFY(merged->sourceInfo().contains("strap.fit"));
    // the main data is not changed
    QVERIFY(!main->hasChannel(ChannelId::HeartRate));

    // taken from the file in place of the main data's
    source.channels << ChannelId::Speed;
    const TelemetrySessionPtr replaced = DataSources::merged(main, {source}, {strap});
    QCOMPARE(replaced->channel(ChannelId::Speed)->valueAt(10.0), 50.0);
    QCOMPARE(replaced->channel(ChannelId::Speed)->count(), 15);

    // a file that could not be read is a warning, not a failure
    const TelemetrySessionPtr missing = DataSources::merged(main, {source}, {nullptr});
    QVERIFY(!missing->hasChannel(ChannelId::HeartRate));
    QCOMPARE(missing->warnings().size(), 1);
    // all before the main data: left out, and said
    source.shift = -100.0;
    source.channels = QStringList{ChannelId::HeartRate};
    const TelemetrySessionPtr early = DataSources::merged(main, {source}, {strap});
    QVERIFY(!early->hasChannel(ChannelId::HeartRate));
    QCOMPARE(early->channel(ChannelId::Speed)->valueAt(10.0), 15.0);
    QCOMPARE(early->warnings().size(), 1);
}

void TestChannel::addedFilesAreLoaded()
{
    QTemporaryDir dir;
    auto gpx = [&](const QString &name, const QString &start, const QString &extra)
    {
        QString points;
        for (int t = 0; t < 10; ++t)
            {
                points += QString("<trkpt lat=\"50.0\" lon=\"%1\"><time>%2</time>%3</trkpt>")
                          .arg(30.0 + t * 0.0001, 0, 'f', 6)
                          .arg(QDateTime::fromString(start, Qt::ISODate).addSecs(t).toString(Qt::ISODate))
                          .arg(extra.arg(120 + t));
            }
        const QString path = dir.filePath(name);
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        file.write(QString("<?xml version=\"1.0\"?><gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
                           "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\"><trk><trkseg>%1"
                           "</trkseg></trk></gpx>").arg(points).toUtf8());
        return path;
    };
    const QString mainPath = gpx("main.gpx", "2025-06-01T12:00:00Z", QString());
    const QString strapPath = gpx("strap.gpx", "2025-06-01T12:00:03Z",
                                  "<extensions><gpxtpx:TrackPointExtension><gpxtpx:hr>%1</gpxtpx:hr>"
                                  "</gpxtpx:TrackPointExtension></extensions>");
    const TelemetrySessionPtr main = TelemetryImporter::loadRecorded(mainPath);
    QVERIFY(main && !main->hasChannel(ChannelId::HeartRate));
    QVector<DataSource> sources{{strapPath, 0.0, false, {ChannelId::HeartRate}},
                                {dir.filePath("gone.gpx"), 0.0, false, {ChannelId::HeartRate}}};
    const TelemetrySessionPtr merged = DataSources::loadAndMerge(main, &sources);
    // the shift followed the clocks
    QCOMPARE(sources.at(0).shift, 3.0);
    QCOMPARE(merged->channel(ChannelId::HeartRate)->valueAt(3.0), 120.0);
    QCOMPARE(merged->warnings().size(), main->warnings().size() + 1);
    // a shift set by hand stays
    sources = {{strapPath, 10.0, true, {ChannelId::HeartRate}}};
    QCOMPARE(DataSources::loadAndMerge(main, &sources)->channel(ChannelId::HeartRate)->valueAt(10.0), 120.0);
    QCOMPARE(sources.at(0).shift, 10.0);
}

QTEST_APPLESS_MAIN(TestChannel)
#include "test_channel.moc"
