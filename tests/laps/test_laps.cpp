#include "laps/lapengine.h"
#include "project/project.h"
#include "telemetry/channelids.h"
#include "telemetry/derivedchannels.h"

#include <QtTest>

#include <cmath>

class TestLaps : public QObject
{
    Q_OBJECT

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kMetersPerDegree = 6371008.8 * kPi / 180.0;
    static constexpr double kLat0 = 49.0;
    static constexpr double kRadius = 100.0;

    // Round a 100 m circle clockwise from 12 o'clock, one lap after another,
    // `durations` seconds each at a steady speed, a sample every 0.5 s. The
    // recording starts 5 s before the top and ends half a lap after the last.
    static TelemetrySessionPtr circuit(const QVector<double> &durations)
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
        auto put = [&](double t, double angle)
        {
            // angle clockwise from north, around the center
            const double north = kRadius * std::cos(angle);
            const double east = kRadius * std::sin(angle);
            lat.append(t, kLat0 + north / kMetersPerDegree);
            lon.append(t, 28.0 + east / (kMetersPerDegree * std::cos(kLat0 * kPi / 180.0)));
        };
        // 5 s of the first lap's pace before the line at 12 o'clock
        const double lead = 5.0;
        double t = 0.0;
        for (; t < lead; t += 0.5)
            {
                put(t, 2.0 * kPi * (t - lead) / durations.first());
            }
        double start = lead;
        for (const double duration : durations)
            {
                for (; t < start + duration - 1e-9; t += 0.5)
                    {
                        put(t, 2.0 * kPi * (t - start) / duration);
                    }
                start += duration;
            }
        // half a lap more, unfinished
        for (; t < start + durations.last() / 2.0; t += 0.5)
            {
                put(t, 2.0 * kPi * (t - start) / durations.last());
            }
        return DerivedChannels::complete(session);
    }

    // across the top of the circle
    static StartFinishLine topLine()
    {
        const double north = (kRadius) / kMetersPerDegree;
        const double halfWidth = 20.0 / (kMetersPerDegree * std::cos(kLat0 * kPi / 180.0));
        return {kLat0 + north, 28.0 - halfWidth, kLat0 + north, 28.0 + halfWidth};
    }

    // 40 m across the circle's edge where it is `north`, `east` of the center, pointing at the center
    static StartFinishLine sectorLine(double north, double east)
    {
        const double length = std::hypot(north, east);
        const double cosLat = std::cos(kLat0 * kPi / 180.0);
        auto point = [&](double r, double *lat, double *lon)
        {
            *lat = kLat0 + north / length * r / kMetersPerDegree;
            *lon = 28.0 + east / length * r / (kMetersPerDegree * cosLat);
        };
        StartFinishLine line;
        point(length - 20.0, &line.lat1, &line.lon1);
        point(length + 20.0, &line.lat2, &line.lon2);
        return line;
    }

    static double at(const TelemetrySessionPtr &s, const char *id, double t, bool *ok = nullptr)
    {
        const Channel *c = s->channel(id);
        return c ? c->valueAt(t, ok) : NAN;
    }

private slots:
    void lapsBetweenCrossings()
    {
        const TelemetrySessionPtr s = circuit({60.0, 50.0, 55.0});
        const QVector<TelemetrySession::Lap> laps = LapEngine::detect(*s, topLine());
        QCOMPARE(laps.size(), 3);
        QVERIFY(std::fabs(laps[0].startTime - 5.0) < 0.05);
        QVERIFY(std::fabs(laps[0].endTime - laps[0].startTime - 60.0) < 0.05);
        QVERIFY(std::fabs(laps[1].endTime - laps[1].startTime - 50.0) < 0.05);
        QVERIFY(std::fabs(laps[2].endTime - laps[2].startTime - 55.0) < 0.05);
        // a line nowhere near the route: no laps
        StartFinishLine away = topLine();
        away.lat1 += 0.01;
        away.lat2 += 0.01;
        QVERIFY(LapEngine::detect(*s, away).isEmpty());
        QVERIFY(LapEngine::detect(*s, StartFinishLine()).isEmpty());
    }

    void onlyOneDirectionAndNoJitter()
    {
        // up to the line, back over it, and over it again within a few seconds
        auto session = std::make_shared<TelemetrySession>();
        Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
        const double norths[] = {-10, -5, 5, 10, 5, -5, -10, -5, 5, 10, 60, 10, -10};
        const double times[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 40, 70, 72};
        for (int i = 0; i < 13; ++i)
            {
                lat.append(times[i], kLat0 + norths[i] / kMetersPerDegree);
                lon.append(times[i], 28.0);
            }
        const double halfWidth = 20.0 / (kMetersPerDegree * std::cos(kLat0 * kPi / 180.0));
        const StartFinishLine line{kLat0, 28.0 - halfWidth, kLat0, 28.0 + halfWidth};
        const QVector<TelemetrySession::Lap> laps = LapEngine::detect(*session, line);
        // northwards at 1.5 s; again at 7.5 s is too soon; southwards at 4.5 s and 71 s do not count:
        // so one crossing, a lap under way
        QCOMPARE(laps.size(), 1);
        QCOMPARE(laps.first().startTime, laps.first().endTime);
        QVERIFY(std::fabs(laps.first().startTime - 1.5) < 1e-6);
    }

    void lapChannels()
    {
        const TelemetrySessionPtr s = LapEngine::apply(circuit({60.0, 50.0, 55.0}), topLine());
        QCOMPARE(s->laps().size(), 3);
        for (const char *id : {ChannelId::Lap, ChannelId::LapTime, ChannelId::PreviousLapTime, ChannelId::BestLapTime,
                               ChannelId::LapDelta, ChannelId::LapCount})
            {
                QVERIFY2(s->derivedChannels().contains(id), id);
            }
        bool ok = true;
        at(s, ChannelId::Lap, 2.0, &ok);
        QVERIFY(!ok);                                   // before the line: no lap yet
        QCOMPARE(at(s, ChannelId::Lap, 30.0), 1.0);
        QCOMPARE(at(s, ChannelId::Lap, 70.0), 2.0);
        QCOMPARE(at(s, ChannelId::Lap, 180.0), 4.0);    // under way after the last finished one
        QCOMPARE(at(s, ChannelId::LapCount, 30.0), 3.0);
        QVERIFY(std::fabs(at(s, ChannelId::LapTime, 35.0) - 30.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::LapTime, 75.0) - 10.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::LapTime, 175.0) - 5.0) < 0.1);
        at(s, ChannelId::PreviousLapTime, 30.0, &ok);
        QVERIFY(!ok);                                   // none finished yet
        QVERIFY(std::fabs(at(s, ChannelId::PreviousLapTime, 70.0) - 60.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::PreviousLapTime, 120.0) - 50.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::BestLapTime, 120.0) - 50.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::BestLapTime, 170.0) - 50.0) < 0.1);   // 55 is not better
        // lap 2 at 50 s against lap 1 at 60 s: halfway round it is 5 s ahead
        QVERIFY(std::fabs(at(s, ChannelId::LapDelta, 65.0 + 25.0) + 5.0) < 0.3);
        // lap 3 at 55 s against lap 2: halfway, 2.5 s behind
        QVERIFY(std::fabs(at(s, ChannelId::LapDelta, 115.0 + 27.5) - 2.5) < 0.3);
        at(s, ChannelId::LapDelta, 30.0, &ok);
        QVERIFY(!ok);                                   // nothing to compare the first lap with
        // which lap that best one was
        QCOMPARE(at(s, ChannelId::BestLapNumber, 70.0), 1.0);
        QCOMPARE(at(s, ChannelId::BestLapNumber, 120.0), 2.0);
        QCOMPARE(at(s, ChannelId::BestLapNumber, 172.0), 2.0);
        // top and average speed start again with every lap: lap 2 is the fast one
        const double fast = 2.0 * kPi * kRadius / 50.0;
        const double last = 2.0 * kPi * kRadius / 55.0;
        QVERIFY(std::fabs(at(s, ChannelId::SpeedLapAvg, 110.0) - fast) < 0.3);
        QVERIFY(std::fabs(at(s, ChannelId::SpeedLapMax, 110.0) - fast) < 0.3);
        QVERIFY(std::fabs(at(s, ChannelId::SpeedLapAvg, 160.0) - last) < 0.3);
        QVERIFY(at(s, ChannelId::SpeedLapMax, 160.0) < fast + 0.3);
    }

    void referenceLap()
    {
        // the fastest of the whole ride when none is chosen: lap 2, 50 s, starting at 65 s
        const TelemetrySessionPtr s = LapEngine::apply(circuit({60.0, 50.0, 55.0}), topLine());
        QVERIFY(std::fabs(s->referenceLap().startTime - 65.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::ReferenceLapTime, 30.0) - 50.0) < 0.1);
        // the first lap is measured too, against a lap still to come: halfway, 5 s behind
        QVERIFY(std::fabs(at(s, ChannelId::ReferenceDelta, 5.0 + 30.0) - 5.0) < 0.3);
        QVERIFY(std::fabs(at(s, ChannelId::ReferenceDelta, 65.0 + 25.0)) < 0.3);       // itself
        QVERIFY(std::fabs(at(s, ChannelId::ReferenceDelta, 115.0 + 27.5) - 2.5) < 0.3);
        // lap 3 chosen: lap 2 is 2.5 s ahead of it halfway
        const TelemetrySessionPtr third = LapEngine::apply(circuit({60.0, 50.0, 55.0}), topLine(), {}, 3);
        QVERIFY(std::fabs(third->referenceLap().startTime - 115.0) < 0.1);
        QVERIFY(std::fabs(at(third, ChannelId::ReferenceDelta, 65.0 + 25.0) + 2.5) < 0.3);
        QVERIFY(std::fabs(at(third, ChannelId::ReferenceLapTime, 30.0) - 55.0) < 0.1);
        // a lap that is not there: the fastest
        QVERIFY(std::fabs(LapEngine::apply(circuit({60.0, 50.0, 55.0}), topLine(), {}, 9)->referenceLap().startTime - 65.0) < 0.1);
        // no laps, no reference
        const TelemetrySessionPtr none = LapEngine::apply(circuit({60.0}), StartFinishLine());
        QVERIFY(none->referenceLap().endTime <= none->referenceLap().startTime);
        QVERIFY(!none->channel(ChannelId::ReferenceDelta));
    }

    void lapsOfALoopLeftAtItsJunction()
    {
        // up a road to a loop of 100 m radius, round it twice in 60 s each, and off it to the west
        // 20 m before getting back the second time, never crossing the line there again
        auto ride = std::make_shared<TelemetrySession>();
        Channel &lat = ride->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = ride->channelForWriting(ChannelId::PositionLon, "deg");
        const double east = kMetersPerDegree * std::cos(kLat0 * kPi / 180.0);
        auto point = [&](double t, double x, double y)
        {
            lat.append(t, kLat0 + y / kMetersPerDegree);
            lon.append(t, 10.0 + x / east);
        };
        for (double t = 0.0; t < 20.0; t += 0.5)
            {
                point(t, 0.0, -200.0 + 10.0 * t);          // the road, northwards
            }
        // the loop from its bottom (0, 0), anticlockwise as seen from above, centre (0, 100)
        double t = 20.0;
        for (; t < 20.0 + 120.0 - 2.0; t += 0.5)
            {
                const double angle = 2.0 * kPi * (t - 20.0) / 60.0;
                point(t, 100.0 * std::sin(angle), 100.0 - 100.0 * std::cos(angle));
            }
        // just short of the bottom the second time: west, away
        const double x0 = (lon.valueAtIndex(lon.count() - 1) - 10.0) * east;
        const double y0 = (lat.valueAtIndex(lat.count() - 1) - kLat0) * kMetersPerDegree;
        for (double away = 0.5; away <= 30.0; away += 0.5)
            {
                point(t + away, x0 - 10.0 * away, y0 - 2.0 * away);
            }
        const TelemetrySessionPtr worked = DerivedChannels::complete(ride);
        // placed a little into the loop; found where it gives both laps
        const StartFinishLine line = LapEngine::findLine(*worked, LapEngine::lineAt(*worked, 30.0), 150.0);
        QVERIFY(line.isValid());
        const TelemetrySessionPtr s = LapEngine::apply(worked, line);
        QCOMPARE(s->laps().size(), 2);
        QVERIFY2(std::fabs(s->laps()[0].endTime - s->laps()[0].startTime - 60.0) < 2.0, qPrintable(QString::number(s->laps()[0].startTime)));
        // the second ends where it came nearest the line, the moments short of a whole lap that the
        // rider turned off before it
        QVERIFY2(s->laps()[1].endTime - s->laps()[1].startTime > 50.0 && s->laps()[1].endTime - s->laps()[1].startTime <= 60.5,
                 qPrintable(QString("%1..%2, %3..%4").arg(s->laps()[0].startTime).arg(s->laps()[0].endTime)
                            .arg(s->laps()[1].startTime).arg(s->laps()[1].endTime)));
        // and no lap after it: the way off the loop is no lap
        QCOMPARE(at(s, ChannelId::LapCount, 10.0), 2.0);
        bool ok = false;
        const double after = s->channel(ChannelId::Lap)->valueAt(t + 20.0, &ok);
        QVERIFY(!ok || after <= 2.0);
    }

    // A ride along `corners` (meters east, north) at 10 m/s, a position every half second.
    static TelemetrySessionPtr ridden(const QVector<QPointF> &corners)
    {
        auto ride = std::make_shared<TelemetrySession>();
        Channel &lat = ride->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = ride->channelForWriting(ChannelId::PositionLon, "deg");
        const double east = kMetersPerDegree * std::cos(kLat0 * kPi / 180.0);
        double t = 0.0;
        for (int i = 1; i < corners.size(); ++i)
            {
                const QPointF from = corners.at(i - 1);
                const QPointF to = corners.at(i);
                const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
                for (double along = 0.0; along < length; along += 5.0, t += 0.5)
                    {
                        const QPointF at = from + (to - from) * (along / length);
                        lat.append(t, kLat0 + at.y() / kMetersPerDegree);
                        lon.append(t, 10.0 + at.x() / east);
                    }
            }
        return DerivedChannels::complete(ride);
    }

    void aLoopRiddenOnceBetweenARoadThereAndBack()
    {
        // north up a road, east along a street to a square loop of 300 m sides, round it once, and
        // back the same street and road
        const TelemetrySessionPtr ride = ridden({{0, 0}, {0, 300}, {100, 300}, {100, 600}, {400, 600}, {400, 300},
                                                 {100, 300}, {0, 300}, {0, 0}});
        // placed on the street to it; found at the loop
        const StartFinishLine line = LapEngine::findLine(*ride, LapEngine::lineAt(*ride, 35.0), 150.0);
        QVERIFY(line.isValid());
        const TelemetrySessionPtr s = LapEngine::apply(ride, line);
        QCOMPARE(s->laps().size(), 1);
        // the loop, 1200 m, not the road and the street with it
        const double length = s->channel(ChannelId::Distance)->valueAt(s->laps().first().endTime)
                              - s->channel(ChannelId::Distance)->valueAt(s->laps().first().startTime);
        QVERIFY2(std::fabs(length - 1200.0) < 150.0, qPrintable(QString::number(length)));
        // and no lap under way after it: the way home is none
        bool ok = false;
        s->channel(ChannelId::Lap)->valueAt(s->duration() - 1.0, &ok);
        QVERIFY(!ok || s->channel(ChannelId::Lap)->valueAt(s->duration() - 1.0) <= 1.0);
        // a line placed by hand on the street, gone out and come back along: the street and the loop
        const StartFinishLine street = LapEngine::lineAt(*ride, 35.0);
        const TelemetrySessionPtr there = LapEngine::apply(ride, street);
        QCOMPARE(there->laps().size(), 1);
        QVERIFY(there->laps().first().endTime - there->laps().first().startTime > 120.0);
        // a road there and back is no lap, by hand or found
        const TelemetrySessionPtr road = ridden({{0, 0}, {0, 800}, {0, 0}});
        QVERIFY(!LapEngine::findLine(*road, LapEngine::lineAt(*road, 30.0), 300.0).isValid());
        QCOMPARE(LapEngine::apply(road, LapEngine::lineAt(*road, 30.0))->laps().size(), 0);
    }

    void recordedLapsWithoutALine()
    {
        auto base = std::make_shared<TelemetrySession>(*circuit({60.0, 50.0}));
        base->addLap({5.0, 65.0});
        base->addLap({65.0, 115.0});
        const TelemetrySessionPtr s = LapEngine::apply(base, StartFinishLine());
        QCOMPARE(s->laps().size(), 2);
        QCOMPARE(at(s, ChannelId::Lap, 70.0), 2.0);
        QVERIFY(std::fabs(at(s, ChannelId::BestLapTime, 120.0) - 50.0) < 1e-9);
        // a recorded lap channel stays without a line, and goes with one
        Channel &lap = base->channelForWriting(ChannelId::Lap, QString(), Channel::Step);
        lap.append(0.0, 7.0);
        QCOMPARE(at(LapEngine::apply(base, StartFinishLine()), ChannelId::Lap, 70.0), 7.0);
        QCOMPARE(at(LapEngine::apply(base, topLine()), ChannelId::Lap, 70.0), 2.0);
        // nothing at all
        // no laps at all: the whole recording is the one stretch the running figures cover
        const TelemetrySessionPtr bare = LapEngine::apply(circuit({60.0}), StartFinishLine());
        QVERIFY(!bare->channel(ChannelId::Lap));
        QVERIFY(std::fabs(at(bare, ChannelId::SpeedLapAvg, 60.0) - 2.0 * kPi * kRadius / 60.0) < 0.3);
    }

    void lineAcrossTheWay()
    {
        const TelemetrySessionPtr s = circuit({60.0});
        // at the top of the circle, riding east: the line runs north-south
        const StartFinishLine line = LapEngine::lineAt(*s, 5.0, 40.0);
        QVERIFY(line.isValid());
        const double east = (line.lon2 - line.lon1) * kMetersPerDegree * std::cos(kLat0 * kPi / 180.0);
        const double north = (line.lat2 - line.lat1) * kMetersPerDegree;
        QVERIFY2(std::fabs(east) < 2.0, qPrintable(QString::number(east)));
        QVERIFY(std::fabs(std::fabs(north) - 40.0) < 1.0);
        QCOMPARE(LapEngine::detect(*s, line).size(), 1);   // and it is crossed
        double distance = 0.0;
        const double t = LapEngine::nearestTime(*s, kLat0 - kRadius / kMetersPerDegree, 28.0, &distance);
        QVERIFY(std::fabs(t - (5.0 + 30.0)) < 0.6);       // the bottom of the circle, half a lap in
        QVERIFY(distance < 1.0);
        QVERIFY(!LapEngine::lineAt(TelemetrySession(), 5.0).isValid());
    }

    void everyPassOfAPlace()
    {
        // three times round: the top is passed at 5, 65, 125 and 185 s
        const TelemetrySessionPtr s = circuit({60.0, 60.0, 60.0});
        const QVector<double> passes = LapEngine::passes(*s, kLat0 + kRadius / kMetersPerDegree, 28.0, 20.0);
        QCOMPARE(passes.size(), 4);
        QVERIFY(std::fabs(passes.at(1) - 65.0) < 1.0);
        QVERIFY(LapEngine::passes(*s, kLat0 + 10.0, 28.0, 20.0).isEmpty());
    }

    void fromOnePlaceToAnother()
    {
        // a line at 6 o'clock is come to once, at 35 s; 9 o'clock follows at 50 s and 3 o'clock at 80 s
        const TelemetrySessionPtr once = circuit({60.0});
        const StartFinishLine start = sectorLine(-kRadius, 0.0);
        QCOMPARE(LapEngine::apply(once, start)->laps().size(), 0);
        const TelemetrySessionPtr s = LapEngine::apply(once, start, {sectorLine(0.0, kRadius), sectorLine(0.0, -kRadius)});
        QCOMPARE(s->laps().size(), 1);
        const TelemetrySession::Lap lap = s->laps().first();
        QVERIFY2(std::fabs(lap.endTime - lap.startTime - 45.0) < 1.0, qPrintable(QString::number(lap.endTime - lap.startTime)));
        QCOMPARE(lap.splits.size(), 1);
        QVERIFY(lap.splits.first() > lap.startTime && lap.splits.first() < lap.endTime);
        // after the finish no lap is under way
        QCOMPARE(at(s, ChannelId::Lap, lap.endTime + 5.0), 1.0);
    }

    void sectors()
    {
        // at 3 o'clock and at 6 o'clock, given in reverse: a quarter, a quarter and a half of each lap
        const QVector<StartFinishLine> lines{sectorLine(-kRadius, 0.0), sectorLine(0.0, kRadius)};
        const TelemetrySessionPtr s = LapEngine::apply(circuit({60.0, 50.0, 55.0}), topLine(), lines);
        QCOMPARE(s->laps().size(), 3);
        QCOMPARE(s->laps().at(0).splits.size(), 2);
        QVERIFY(std::fabs(s->laps().at(0).splits.at(0) - 20.0) < 0.05);
        QVERIFY(std::fabs(s->laps().at(0).splits.at(1) - 35.0) < 0.05);
        QVERIFY(std::fabs(s->laps().at(1).splits.at(0) - 77.5) < 0.05);
        for (const char *id : {ChannelId::Sector, ChannelId::SectorTime, ChannelId::PreviousSectorTime,
                               ChannelId::SectorDelta})
            {
                QVERIFY2(s->derivedChannels().contains(id), id);
            }
        QCOMPARE(at(s, ChannelId::Sector, 10.0), 1.0);
        QCOMPARE(at(s, ChannelId::Sector, 25.0), 2.0);
        QCOMPARE(at(s, ChannelId::Sector, 40.0), 3.0);
        QCOMPARE(at(s, ChannelId::Sector, 66.0), 1.0);
        QCOMPARE(at(s, ChannelId::Sector, 185.0), 2.0);   // the lap under way has its sectors too
        QVERIFY(std::fabs(at(s, ChannelId::SectorTime, 30.0) - 10.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::PreviousSectorTime, 36.0) - 15.0) < 0.1);
        bool ok = true;
        at(s, ChannelId::PreviousSectorTime, 10.0, &ok);
        QVERIFY(!ok);
        // the second time through sector 1: 12.5 s against 15
        QVERIFY(std::fabs(at(s, ChannelId::SectorDelta, 78.0) + 2.5) < 0.1);
        // a line the route never crosses: no sector times
        const TelemetrySessionPtr missed
            = LapEngine::apply(circuit({60.0}), topLine(), {sectorLine(0.0, kRadius), sectorLine(0.0, 3.0 * kRadius)});
        QVERIFY(missed->laps().at(0).splits.isEmpty());
        QVERIFY(!missed->channel(ChannelId::PreviousSectorTime));
    }

    void lapsCompared()
    {
        const TelemetrySessionPtr s = LapEngine::apply(circuit({60.0, 50.0}), topLine());
        QCOMPARE(s->laps().size(), 2);
        const TelemetrySession::Lap first = s->laps().at(0);
        const TelemetrySession::Lap second = s->laps().at(1);
        const double round = 2.0 * kPi * kRadius;
        // a point every 5 m at the first lap's steady speed
        const QVector<QPointF> speed = LapEngine::byDistance(*s, first, ChannelId::Speed);
        QVERIFY(std::abs(speed.size() - int(round / 5.0)) <= 2);
        QVERIFY(std::fabs(speed.at(speed.size() / 2).y() - round / 60.0) < 0.3);
        QVERIFY(speed.first().x() < 1.0);
        // the second lap gains on the first all the way round: 5 s by halfway, 10 s at the line
        const QVector<QPointF> delta = LapEngine::deltaByDistance(*s, first, second);
        QVERIFY(std::abs(delta.size() - int(round / 5.0)) <= 2);
        QVERIFY(std::fabs(delta.first().y()) < 0.1);
        const QPointF half = delta.at(delta.size() / 2);
        QVERIFY2(std::fabs(half.y() + 10.0 * half.x() / round) < 0.3, qPrintable(QString::number(half.y())));
        QVERIFY(std::fabs(delta.last().y() + 10.0 * delta.last().x() / round) < 0.3);
        // and against itself, nothing
        for (const QPointF &p : LapEngine::deltaByDistance(*s, first, first))
            {
                QVERIFY(std::fabs(p.y()) < 1e-9);
            }
        QVERIFY(LapEngine::byDistance(*s, first, "nothing").isEmpty());
    }

    void projectKeepsTheLine()
    {
        Project project;
        project.startFinish = topLine();
        project.sectors = {sectorLine(0.0, kRadius)};
        project.cuts = {{12.5, 30.0}, {40.0, 41.0}};
        project.surfaces = {{1200.0, 3400.5, "Hard gravel", 1.8}};
        project.wind = {true, 4.5, 270.0};
        project.dataEdits = {{ChannelId::Speed, 3.0, 4.5}};
        Project loaded;
        QVERIFY(ProjectFile::fromJson(ProjectFile::toJson(project, "/tmp"), "/tmp", &loaded));
        QVERIFY(loaded.startFinish == project.startFinish);
        QCOMPARE(loaded.sectors.size(), 1);
        QVERIFY(loaded.sectors.first() == project.sectors.first());
        QVERIFY(loaded.cuts == project.cuts);
        QVERIFY(loaded.surfaces == project.surfaces);
        QVERIFY(loaded.wind == project.wind);
        QVERIFY(loaded.dataEdits == project.dataEdits);
        Project none;
        QVERIFY(ProjectFile::fromJson(ProjectFile::toJson(Project(), "/tmp"), "/tmp", &none));
        QVERIFY(!none.startFinish.isValid());
    }
};

QTEST_MAIN(TestLaps)
#include "test_laps.moc"
