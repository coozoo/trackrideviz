#include "sync/syncmap.h"
#include "sync/videosync.h"
#include "telemetry/channelids.h"

#include <QtTest>

#include <cmath>

class TestSync : public QObject
{
    Q_OBJECT

private:
    // 90 minutes of data starting 2025-07-15 04:47:11 UTC
    static TelemetrySession makeSession()
    {
        TelemetrySession session;
        session.setStartUtc(QDateTime::fromString("2025-07-15T04:47:11Z", Qt::ISODate));
        Channel &speed = session.channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, 1.0);
        speed.append(5400.0, 1.0);
        return session;
    }

    static QDateTime utc(const QString &iso)
    {
        return QDateTime::fromString(iso, Qt::ISODate);
    }

private slots:
    void syncMapWithABreak()
    {
        // two recordings joined: the first 100 s long, then a break of 60 s
        const SyncMap sync(30.0, {{100.0, 60.0}});
        QCOMPARE(sync.telemetryTime(10.0), 40.0);
        QCOMPARE(sync.telemetryTime(99.0), 129.0);
        QCOMPARE(sync.telemetryTime(100.0), 190.0);
        QCOMPARE(sync.videoTime(40.0), 10.0);
        QCOMPARE(sync.videoTime(190.0), 100.0);
        QCOMPARE(sync.videoTime(200.0), 110.0);
        QCOMPARE(sync.videoTime(150.0), 100.0);   // in the break: no picture, the next part
        // one recording: a plain offset
        QCOMPARE(SyncMap(-5.0).videoTime(0.0), 5.0);
    }

    void fileNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected"); // empty = no time in the name

        QTest::newRow("action camera") << "20250715_074932A.mp4" << "2025-07-15T07:49:32Z";
        QTest::newRow("phone") << "VID_20250715_074932.mp4" << "2025-07-15T07:49:32Z";
        QTest::newRow("dashes") << "2025-07-15 07-49-32.mkv" << "2025-07-15T07:49:32Z";
        QTest::newRow("iso") << "ride 2025-07-15T07:49:32.mov" << "2025-07-15T07:49:32Z";
        QTest::newRow("with path") << "/videos/2024/20250715_074932A.mp4" << "2025-07-15T07:49:32Z";
        QTest::newRow("gopro counter") << "GX010042.MP4" << "";
        QTest::newRow("plain name") << "My Video.mp4" << "";
        QTest::newRow("date only") << "ride20250715.mp4" << "";
        QTest::newRow("not a date") << "20259915_074932.mp4" << "";
        QTest::newRow("not a time") << "20250715_256199.mp4" << "";
        QTest::newRow("longer number") << "1720250715074932123.mp4" << "";
    }

    void fileNames()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);
        const QDateTime result = VideoSync::wallClockFromFileName(name);
        if (expected.isEmpty())
            {
                QVERIFY(!result.isValid());
            }
        else
            {
                QCOMPARE(result, utc(expected));
            }
    }

    void usesUtcOffsetOfTheData()
    {
        TelemetrySession session = makeSession();
        session.setUtcOffsetSeconds(3 * 3600);
        // the fallback is deliberately wrong: it must not be used
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-15T07:49:32Z"), 1080.0, -8 * 3600);
        QVERIFY(result.ok);
        QCOMPARE(result.offsetSeconds, 141.0);
        QVERIFY(result.explanation.contains("UTC+3:00"));
    }

    void fallsBackToGivenOffset()
    {
        const TelemetrySession session = makeSession();
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-15T07:49:32Z"), 1080.0, 3 * 3600);
        QVERIFY(result.ok);
        QCOMPARE(result.offsetSeconds, 141.0);
    }

    void videoMayStartBeforeTheData()
    {
        TelemetrySession session = makeSession();
        session.setUtcOffsetSeconds(3 * 3600);
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-15T07:46:11Z"), 1080.0, 0);
        QVERIFY(result.ok);
        QCOMPARE(result.offsetSeconds, -60.0);
    }

    // An export that wrote local time labelled as UTC: the data claims to
    // start at 07:47:11Z and states no offset.
    void snapsWhenTimeZoneIsWrong()
    {
        TelemetrySession session = makeSession();
        session.setStartUtc(utc("2025-07-15T07:47:11Z"));
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-15T07:49:32Z"), 1080.0, 3 * 3600);
        QVERIFY(result.ok);
        QCOMPARE(result.offsetSeconds, 141.0);
        QVERIFY(result.explanation.contains("180"));
    }

    void snapsToHalfHourZones()
    {
        TelemetrySession session = makeSession();
        // camera clock on UTC+5:30, computer on UTC
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-15T10:19:32Z"), 60.0, 0);
        QVERIFY(result.ok);
        // some shift by whole quarter hours that overlaps the 90 minutes of data
        QVERIFY(result.offsetSeconds >= -60.0 && result.offsetSeconds <= 5400.0);
        QCOMPARE(std::fmod(result.offsetSeconds - 141.0, 900.0), 0.0);
    }

    void differentDayDoesNotMatch()
    {
        const TelemetrySession session = makeSession();
        const VideoSync::Result result = VideoSync::match(session, utc("2025-07-17T07:49:32Z"), 1080.0, 0);
        QVERIFY(!result.ok);
        QVERIFY(!result.explanation.isEmpty());
    }

    void needsAbsoluteTime()
    {
        TelemetrySession session = makeSession();
        session.setStartUtc(QDateTime());
        QVERIFY(!VideoSync::match(session, utc("2025-07-15T07:49:32Z"), 1080.0, 0).ok);
        QVERIFY(!VideoSync::match(makeSession(), QDateTime(), 1080.0, 0).ok);
    }
};

QTEST_APPLESS_MAIN(TestSync)
#include "test_sync.moc"
