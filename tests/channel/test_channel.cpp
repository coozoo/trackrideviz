#include <QtTest>

#include "telemetry/channel.h"
#include "telemetry/telemetrysession.h"

class TestChannel : public QObject
{
    Q_OBJECT

private slots:
    void emptyChannel();
    void linearInterpolation();
    void stepInterpolation();
    void outOfRangeClampsAndReportsNotOk();
    void rejectsNonIncreasingTime();
    void irregularSampleRate();
    void sessionChannelReferencesStayValid();
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

QTEST_APPLESS_MAIN(TestChannel)
#include "test_channel.moc"
