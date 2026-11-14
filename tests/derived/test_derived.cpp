#include "telemetry/channelids.h"
#include "telemetry/dataedits.h"
#include "maps/terrain.h"
#include "maps/tilestore.h"
#include "telemetry/derivedchannels.h"

#include <QtTest>

#include <cmath>

class TestDerived : public QObject
{
    Q_OBJECT

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kMetresPerDegree = 6371008.8 * kPi / 180.0;

    // Positions along `place` (seconds -> north and east metres), sampled every `step` s.
    template <typename Place>
    static std::shared_ptr<TelemetrySession> ride(double seconds, double step, Place place, bool elevation = false)
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &lat = session->channelForWriting(ChannelId::PositionLat, "deg");
        Channel &lon = session->channelForWriting(ChannelId::PositionLon, "deg");
        Channel *alt = elevation ? &session->channelForWriting(ChannelId::Elevation, "m") : nullptr;
        const double lat0 = 49.0;
        for (double t = 0.0; t <= seconds + 1e-9; t += step)
            {
                const QPointF p = place(t);   // x north, y east
                lat.append(t, lat0 + p.x() / kMetresPerDegree);
                lon.append(t, 28.0 + p.y() / (kMetresPerDegree * std::cos(lat0 * kPi / 180.0)));
                if (alt)
                    {
                        alt->append(t, 200.0 + p.x() * 0.05);   // 5 % up, northwards
                    }
            }
        return session;
    }

    static double at(const TelemetrySessionPtr &s, const char *id, double t)
    {
        const Channel *c = s->channel(id);
        return c ? c->valueAt(t) : NAN;
    }

private slots:
    void editTakesOutAGlitch()
    {
        // north at 10 m/s, with one position 300 m off to the east at 50 s
        auto recorded = ride(100.0, 1.0, [](double t) { return QPointF(10.0 * t, 0.0); });
        auto raw = std::make_shared<TelemetrySession>(*recorded);
        {
            Channel lat = *raw->channel(ChannelId::PositionLat);
            Channel lon = *raw->channel(ChannelId::PositionLon);
            raw->removeChannel(ChannelId::PositionLat);
            raw->removeChannel(ChannelId::PositionLon);
            Channel &la = raw->channelForWriting(ChannelId::PositionLat, "deg");
            Channel &lo = raw->channelForWriting(ChannelId::PositionLon, "deg");
            for (int i = 0; i < lat.count(); ++i)
                {
                    la.append(lat.timeAt(i), lat.valueAtIndex(i));
                    lo.append(lon.timeAt(i), lon.valueAtIndex(i) + (lat.timeAt(i) == 50.0 ? 0.004 : 0.0));
                }
        }
        const double glitched = at(DerivedChannels::complete(raw), ChannelId::Speed, 50.0);
        QVERIFY2(std::fabs(glitched - 10.0) > 3.0, qPrintable(QString::number(glitched)));
        // an edit of the latitude takes the longitude with it
        const QVector<DataEdit> edits{{ChannelId::PositionLat, 49.5, 50.5}};
        const TelemetrySessionPtr edited = DataEdits::apply(raw, edits);
        QCOMPARE(edited->channel(ChannelId::PositionLon)->count(), raw->channel(ChannelId::PositionLon)->count() - 1);
        QCOMPARE(raw->channel(ChannelId::PositionLon)->count(), 101);   // the recording is not touched
        const double fixed = at(DerivedChannels::complete(edited), ChannelId::Speed, 50.0);
        QVERIFY2(std::fabs(fixed - 10.0) < 0.5, qPrintable(QString::number(fixed)));
        QCOMPARE(DataEdits::apply(raw, {}), TelemetrySessionPtr(raw));
        QVERIFY(DataEdits::restored(edits, ChannelId::PositionLon, 40.0, 60.0).isEmpty());
        QCOMPARE(DataEdits::restored(edits, ChannelId::Speed, 40.0, 60.0).size(), 1);
    }

    void straightClimb()
    {
        // north at 10 m/s for 5 minutes, climbing 5 %
        const TelemetrySessionPtr original = ride(300.0, 1.0, [](double t) { return QPointF(10.0 * t, 0.0); }, true);
        const TelemetrySessionPtr s = DerivedChannels::complete(original);
        QVERIFY(s != original);
        QVERIFY(!original->channel(ChannelId::Distance));     // the original is left alone
        const QStringList derived = s->derivedChannels();
        for (const char *id : {ChannelId::Distance, ChannelId::Speed, ChannelId::Heading, ChannelId::Grade,
                               ChannelId::GradeAngle, ChannelId::TotalAscent, ChannelId::TotalDescent,
                               ChannelId::VerticalSpeed, ChannelId::GForceLongitudinal, ChannelId::GForceLateral})
            {
                QVERIFY2(derived.contains(id), id);
            }
        QVERIFY(std::fabs(at(s, ChannelId::Distance, 300.0) - 3000.0) < 3.0);
        QVERIFY(std::fabs(at(s, ChannelId::Speed, 150.0) - 10.0) < 0.05);
        const double heading = at(s, ChannelId::Heading, 150.0);
        QVERIFY2(heading < 0.5 || heading > 359.5, qPrintable(QString::number(heading)));
        QVERIFY(std::fabs(at(s, ChannelId::Grade, 150.0) - 5.0) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::GradeAngle, 150.0) - std::atan(0.05) * 180.0 / kPi) < 0.1);
        QVERIFY(std::fabs(at(s, ChannelId::VerticalSpeed, 150.0) - 0.5) < 0.02);
        QVERIFY(std::fabs(at(s, ChannelId::TotalAscent, 300.0) - 150.0) < 3.0);
        QCOMPARE(at(s, ChannelId::TotalDescent, 300.0), 0.0);
        QVERIFY(std::fabs(at(s, ChannelId::GForceLongitudinal, 150.0)) < 0.01);
        QVERIFY(std::fabs(at(s, ChannelId::GForceLateral, 150.0)) < 0.01);
        QCOMPARE(s->channel(ChannelId::Grade)->unit(), QString("%"));
        // the small sums: 10 m/s is 100 s to the kilometre; from 200 m up to 350 m, 3 km in all
        QCOMPARE(at(s, ChannelId::ElapsedTime, 150.0), 150.0);
        QVERIFY(std::fabs(at(s, ChannelId::Pace, 150.0) - 100.0) < 0.5);
        QVERIFY(std::fabs(at(s, ChannelId::ElevationMax, 10.0) - 350.0) < 0.5);
        QVERIFY(std::fabs(at(s, ChannelId::ElevationMin, 290.0) - 200.0) < 0.5);
        QVERIFY(std::fabs(at(s, ChannelId::DistanceTotal, 10.0) - 3000.0) < 3.0);
        QCOMPARE(at(s, ChannelId::FallingSpeed, 150.0), 0.0);
        QVERIFY(!s->channel(ChannelId::GlideRatio));          // never going down
        QVERIFY(std::fabs(at(s, ChannelId::Braking, 150.0)) < 0.01);
    }

    void elevationFromTheTerrain()
    {
        // tiles of a terrain where the height is 300 m and rises a metre with every pixel east
        QTemporaryDir dir;
        TileStore *store = TileStore::terrain();
        store->setFolder(dir.path());
        const double world = 256.0 * 4096.0;
        const int tileX = int((28.0 + 180.0) / 360.0 * 4096.0);
        const int tileY = int((180.0 - TileStore::mercatorLatitude(49.0)) / 360.0 * 4096.0);
        auto write = [&](int x, int y)
        {
            QImage tile(256, 256, QImage::Format_RGB32);
            for (int px = 0; px < 256; ++px)
                {
                    const int coded = 300 + (x - tileX) * 256 + px + 32768;
                    for (int py = 0; py < 256; ++py)
                        {
                            tile.setPixel(px, py, qRgb(coded / 256, coded % 256, 0));
                        }
                }
            const QString path = store->tilePath(12, x, y);
            QVERIFY(QDir().mkpath(QFileInfo(path).path()));
            QVERIFY(tile.save(path));
        };
        // a ride north from the middle of a tile's width: 3 km is about 170 pixels, into the tile above
        const double lon0 = (tileX + 0.5) * 256.0 / world * 360.0 - 180.0;
        auto make = [&](bool elevation)
        {
            auto session = ride(300.0, 1.0, [](double t) { return QPointF(10.0 * t, 0.0); }, elevation);
            auto moved = std::make_shared<TelemetrySession>();
            Channel &lat = moved->channelForWriting(ChannelId::PositionLat, "deg");
            Channel &lon = moved->channelForWriting(ChannelId::PositionLon, "deg");
            const Channel *from = session->channel(ChannelId::PositionLat);
            for (int i = 0; i < from->count(); ++i)
                {
                    lat.append(from->timeAt(i), from->valueAtIndex(i));
                    lon.append(from->timeAt(i), lon0);
                }
            if (elevation)
                {
                    Channel &alt = moved->channelForWriting(ChannelId::Elevation, "m");
                    alt.append(0.0, 1000.0);
                    alt.append(300.0, 1000.0);
                }
            return moved;
        };
        const TelemetrySessionPtr bare = make(false);

        // nothing on disk yet: left alone, and said to be waiting
        bool waiting = false;
        QCOMPARE(Terrain::apply(bare, Terrain::WhenMissing, &waiting), bare);
        QVERIFY(waiting);
        for (int y = tileY - 1; y <= tileY + 1; ++y)
            {
                write(tileX, y);
            }
        // half way between pixels 127 and 128 of the tile: 427.5 m, all the way north
        double height = 0.0;
        QVERIFY(Terrain::elevationAt(49.0, lon0, &height));
        QVERIFY2(std::fabs(height - 427.5) < 0.01, qPrintable(QString::number(height)));
        const TelemetrySessionPtr filled = Terrain::apply(bare, Terrain::WhenMissing, &waiting);
        QVERIFY(!waiting);
        QVERIFY(filled != bare);
        QVERIFY(std::fabs(at(filled, ChannelId::Elevation, 150.0) - 427.5) < 0.01);
        QVERIFY(filled->derivedChannels().contains(ChannelId::Elevation));
        QCOMPARE(Terrain::apply(bare, Terrain::Never), bare);
        // a recorded elevation stays unless the terrain is asked for
        const TelemetrySessionPtr recorded = make(true);
        QCOMPARE(Terrain::apply(recorded, Terrain::WhenMissing), recorded);
        QVERIFY(std::fabs(at(Terrain::apply(recorded, Terrain::Always), ChannelId::Elevation, 150.0) - 427.5) < 0.01);
        store->setFolder(dir.filePath("none"));
    }

    void powerAveragedOverTheLastSeconds()
    {
        // 100 W for a minute, then 400 W
        const auto original = ride(120.0, 1.0, [](double t) { return QPointF(10.0 * t, 0.0); });
        Channel &power = original->channelForWriting(ChannelId::Power, "W");
        for (int t = 0; t <= 120; ++t)
            {
                power.append(t, t < 60 ? 100.0 : 400.0);
            }
        const TelemetrySessionPtr s = DerivedChannels::complete(original);
        QCOMPARE(at(s, ChannelId::Power3s, 50.0), 100.0);
        QCOMPARE(at(s, ChannelId::Power30s, 50.0), 100.0);
        // three seconds on the short one has caught up, the long one is a tenth of the way
        QCOMPARE(at(s, ChannelId::Power3s, 63.0), 400.0);
        QVERIFY(std::fabs(at(s, ChannelId::Power30s, 63.0) - (100.0 + 300.0 * 4.0 / 30.0)) < 11.0);
        QCOMPARE(at(s, ChannelId::Power30s, 100.0), 400.0);
    }

    void circleGivesSidewaysG()
    {
        // clockwise (a right turn) round a 50 m circle at 15 m/s: v^2/r = 4.5 m/s^2
        const double r = 50.0;
        const double w = 15.0 / r;
        const TelemetrySessionPtr s = DerivedChannels::complete(
                                          ride(120.0, 0.1, [=](double t) { return QPointF(r * std::cos(w * t), r * std::sin(w * t)); }));
        const double g = at(s, ChannelId::GForceLateral, 60.0);
        QVERIFY2(std::fabs(g - 4.5 / 9.80665) < 0.03, qPrintable(QString::number(g)));
        QVERIFY(std::fabs(at(s, ChannelId::GForceLongitudinal, 60.0)) < 0.02);
        // counter-clockwise: to the left
        const TelemetrySessionPtr left = DerivedChannels::complete(
                                             ride(120.0, 0.1, [=](double t) { return QPointF(r * std::cos(w * t), -r * std::sin(w * t)); }));
        QVERIFY(std::fabs(at(left, ChannelId::GForceLateral, 60.0) + 4.5 / 9.80665) < 0.03);
    }

    void brakingGivesForwardG()
    {
        // 20 m/s, braking at 5 m/s^2 from 10 s until standing at 14 s
        auto position = [](double t)
        {
            if (t < 10.0)
                {
                    return QPointF(20.0 * t, 0.0);
                }
            const double b = std::min(t - 10.0, 4.0);
            return QPointF(200.0 + 20.0 * b - 2.5 * b * b, 0.0);
        };
        const TelemetrySessionPtr s = DerivedChannels::complete(ride(30.0, 0.1, position));
        const double g = at(s, ChannelId::GForceLongitudinal, 12.0);
        QVERIFY2(std::fabs(g + 5.0 / 9.80665) < 0.05, qPrintable(QString::number(g)));
        // standing still: no heading change, no sideways g
        QCOMPARE(at(s, ChannelId::GForceLateral, 25.0), 0.0);
        QVERIFY(at(s, ChannelId::Speed, 25.0) < 0.1);
    }

    void recordedChannelsAreKeptAndPausesSkipped()
    {
        auto session = ride(100.0, 1.0, [](double t) { return QPointF(5.0 * t, 0.0); }, true);
        Channel &grade = session->channelForWriting(ChannelId::Grade, "%");
        grade.append(0.0, 1.0);
        grade.append(100.0, 1.0);
        session->addPause({40.0, 60.0});
        const TelemetrySessionPtr s = DerivedChannels::complete(session);
        QVERIFY(!s->derivedChannels().contains(ChannelId::Grade));
        QCOMPARE(at(s, ChannelId::Grade, 50.0), 1.0);
        // the angle comes from the recorded grade
        QVERIFY(std::fabs(at(s, ChannelId::GradeAngle, 20.0) - std::atan(0.01) * 180.0 / kPi) < 1e-6);
        // nothing worked out inside the pause
        const Channel *vertical = s->channel(ChannelId::VerticalSpeed);
        for (int i = 0; i < vertical->count(); ++i)
            {
                QVERIFY(vertical->timeAt(i) <= 40.0 || vertical->timeAt(i) >= 60.0);
            }
    }

    void nothingToAdd()
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &hr = session->channelForWriting(ChannelId::HeartRate, "bpm");
        hr.append(0.0, 100.0);
        hr.append(1.0, 110.0);
        const TelemetrySessionPtr same = session;
        QCOMPARE(DerivedChannels::complete(same), same);
        QCOMPARE(DerivedChannels::complete(nullptr), TelemetrySessionPtr());
    }

    void glitchesAreNotDistance()
    {
        // one position 5 km off for a second
        const TelemetrySessionPtr s = DerivedChannels::complete(ride(100.0, 1.0, [](double t)
        {
            return QPointF(t == 50.0 ? 5000.0 : 10.0 * t, 0.0);
        }));
        QVERIFY(at(s, ChannelId::Distance, 100.0) < 1100.0);
    }
};

QTEST_MAIN(TestDerived)
#include "test_derived.moc"
