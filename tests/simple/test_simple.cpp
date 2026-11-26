#include "simple/channelgraph.h"
#include "simple/routeview.h"
#include "simple/simplecontroller.h"
#include "gauge/gaugelibrary.h"
#include "telemetry/channelids.h"
#include "overlay/templatelibrary.h"
#include "settings/bundleddata.h"
#include "video/ffmpegtools.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QTest>

class TestSimple : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_video;   // made by ffmpeg in initTestCase; empty when ffmpeg is missing
    QString m_data;

private slots:
    void initTestCase()
    {
        // settings of their own, not the user's
        QCoreApplication::setOrganizationName("trackrideviz-test");
        QCoreApplication::setApplicationName("tst_simple");
        QVERIFY(m_dir.isValid());
        TemplateLibrary::setUserFolder(m_dir.filePath("templates"));
        BundledData::setFolder(QStringLiteral(RTV_TEST_DATA));
        // a ride of 10 s, a point a second
        m_data = m_dir.filePath("ride.gpx");
        QFile gpx(m_data);
        QVERIFY(gpx.open(QIODevice::WriteOnly));
        QByteArray text = "<?xml version=\"1.0\"?><gpx version=\"1.1\" creator=\"test\"><trk><trkseg>";
        for (int i = 0; i <= 10; ++i)
            {
                text += QString("<trkpt lat=\"%1\" lon=\"30.5\"><time>2025-07-15T07:47:%2Z</time></trkpt>")
                        .arg(50.45 + i * 1e-4, 0, 'f', 6).arg(10 + i, 2, 10, QChar('0')).toUtf8();
            }
        gpx.write(text + "</trkseg></trk></gpx>");
        gpx.close();
        if (FfmpegTools::versionLine("ffmpeg").isEmpty())
            {
                return;
            }
        const QString path = m_dir.filePath("ride.mp4");
        const FfmpegTools::RunResult made = FfmpegTools::run("ffmpeg",
        {
            "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "color=c=gray:s=320x240:r=30:d=2",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", path
        }, 60000);
        if (made.started && made.exitCode == 0)
            {
                m_video = path;
            }
    }

    // What the window does from start to end.
    void videoDataLookExport()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        QVERIFY(!simple.canGoBack());
        QSignalSpy busy(&simple, &SimpleController::busyChanged);

        // the video: probed; with no data of its own, the data is asked for
        simple.openVideo(QUrl::fromLocalFile(m_video));
        QCOMPARE(simple.videoName(), QString("ride.mp4"));
        QTRY_VERIFY_WITH_TIMEOUT(!simple.busy() && !simple.message().isEmpty(), 20000);
        QVERIFY(!simple.hasData());
        QVERIFY(qAbs(simple.aspect() - 320.0 / 240.0) < 1e-9);

        // the data: read, a look laid over it, in step as well as the clocks can say
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.dataName(), QString("ride.gpx"));
        QVERIFY(!simple.syncText().isEmpty());
        if (!simple.templateNames().isEmpty())
            {
                QVERIFY(simple.templateIndex() >= 0);
                QVERIFY(simple.overlay()->count() > 0);
            }

        // moved by hand
        const double before = simple.syncOffset();
        simple.nudgeSync(0.5);
        QVERIFY(qAbs(simple.syncOffset() - before - 0.5) < 1e-9);
        QVERIFY(qAbs(simple.telemetryTime(1000) - (1.0 + simple.syncOffset())) < 1e-9);

        // the export: beside the video, named after it
        simple.startExport();
        QTRY_VERIFY_WITH_TIMEOUT(simple.exporting(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(!simple.exporting(), 120000);
        QVERIFY2(!simple.exportedUrl().isEmpty(), qPrintable(simple.exportText()));
        const QFileInfo made(simple.exportedUrl().toLocalFile());
        QVERIFY(made.exists() && made.size() > 0);
        QCOMPARE(made.absolutePath(), QFileInfo(m_video).absolutePath());
        QVERIFY(made.fileName().startsWith("ride_overlay."));

        // a second export never replaces the first
        simple.startExport();
        QTRY_VERIFY_WITH_TIMEOUT(simple.exporting(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(!simple.exporting(), 120000);
        QVERIFY(simple.exportedUrl().toLocalFile() != made.filePath());
        QVERIFY(made.exists());
    }

    // A video and data opened together, as from the command line: the data is not lost to a
    // look for data in the video.
    void videoAndDataTogether()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.dataName(), QString("ride.gpx"));
    }

    // Gauges added, found under a finger, moved, changed and taken off.
    void gaugesByHand()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        const int before = simple.gaugeCount();
        QVERIFY(!simple.gaugeIds().isEmpty());
        simple.addGauge(simple.gaugeIds().first());
        QCOMPARE(simple.gaugeCount(), before + 1);
        const int added = before;

        // on a picture of 320 x 240: under its middle it is found
        const QRectF where = simple.gaugeRect(added, 320, 240);
        QVERIFY(where.isValid());
        QCOMPARE(simple.gaugeAt(where.center().x(), where.center().y(), 320, 240), added);

        // moved by a finger, then let go: drawn where it was put, and kept there
        simple.moveGauge(added, 0.05, 0.1, false);
        QVERIFY(qAbs(simple.gaugeRect(added, 320, 240).x() - 16.0) < 0.5);
        simple.moveGauge(added, 0.05, 0.1, true);
        QVERIFY(qAbs(simple.gaugeRect(added, 320, 240).y() - 24.0) < 0.5);
        // never off the picture
        simple.moveGauge(added, 5.0, -3.0, true);
        const QRectF held = simple.gaugeRect(added, 320, 240);
        QVERIFY(held.right() <= 320.5 && held.top() >= -0.5);

        // its properties
        const QVariantMap properties = simple.gaugeProperties(added);
        QVERIFY(!properties.value("name").toString().isEmpty());
        QVERIFY(properties.value("channelIds").toStringList().contains(properties.value("channel").toString()));
        simple.resizeGauge(added, 0.4);
        QVERIFY(qAbs(simple.gaugeProperties(added).value("height").toDouble() - 0.4) < 1e-9);
        simple.setGaugeOpacity(added, 0.5);
        QCOMPARE(simple.gaugeProperties(added).value("opacity").toDouble(), 0.5);
        simple.setGaugeTextScale(added, 9.0);
        QCOMPARE(simple.gaugeProperties(added).value("textScale").toDouble(), 3.0);

        simple.removeGauge(added);
        QCOMPARE(simple.gaugeCount(), before);
    }

    // The full window's project: its gauges and its step kept, not a dash and the clocks.
    void fromTheFullWindow()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        Project project;
        project.videoPath = m_video;
        project.telemetryPath = m_data;
        OverlayItem item;
        item.gaugeId = "builtin:speedometer";
        item.x = 0.3;
        item.y = 0.2;
        project.overlay = {item};
        // laid out for this video's shape, as the full window leaves it
        project.layoutAspect = 320.0 / 240.0;
        project.syncOffset = 3.25;
        project.syncIsManual = true;
        SimpleController simple(true);
        simple.openProject(project);
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.gaugeCount(), 1);
        QCOMPARE(simple.project().overlay.first().x, 0.3);
        QCOMPARE(simple.syncOffset(), 3.25);
        QCOMPARE(simple.videoName(), QString("ride.mp4"));
        // what is done here goes back
        simple.nudgeSync(0.5);
        QCOMPARE(simple.project().syncOffset, 3.75);
        // the clocks asked for again: no longer by hand
        simple.syncByClocks();
        QVERIFY(!simple.project().syncIsManual);
    }

    // A gauge's shape set free and its width its own; a route's map, close-up and 3D.
    void gaugeOptions()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        simple.addGauge("builtin:speedometer");
        const int dial = simple.gaugeCount() - 1;
        simple.moveGauge(dial, 0.1, 0.1, true);
        const QRectF kept = simple.gaugeRect(dial, 320, 240);
        simple.setGaugeKeepShape(dial, false);
        QVERIFY(!simple.gaugeProperties(dial).value("keepShape").toBool());
        // freed where it is: nothing moved
        QVERIFY(qAbs(simple.gaugeRect(dial, 320, 240).width() - kept.width()) < 0.5);
        simple.setGaugeWidth(dial, 0.5);
        QVERIFY(qAbs(simple.gaugeRect(dial, 320, 240).width() - 160.0) < 0.5);
        QVERIFY(qAbs(simple.gaugeProperties(dial).value("width").toDouble() - 0.5) < 1e-6);
        QVERIFY(qAbs(simple.gaugeRect(dial, 320, 240).height() - kept.height()) < 0.5);
        simple.setGaugeKeepShape(dial, true);
        QVERIFY(qAbs(simple.gaugeRect(dial, 320, 240).width() - kept.width()) < 0.5);
        simple.setGaugeUnits(dial, OverlayItem::ImperialUnits);
        QCOMPARE(simple.gaugeProperties(dial).value("units").toInt(), int(OverlayItem::ImperialUnits));

        // a gauge with a route
        int route = -1;
        for (const QString &id : simple.gaugeIds())
            {
                simple.addGauge(id);
                if (simple.gaugeProperties(simple.gaugeCount() - 1).value("hasRoute").toBool())
                    {
                        route = simple.gaugeCount() - 1;
                        break;
                    }
                simple.removeGauge(simple.gaugeCount() - 1);
            }
        if (route < 0)
            {
                QSKIP("no gauge with a route");
            }
        QVERIFY(simple.setGaugeCloseUp(route, 600, 0, 0, 0));
        QCOMPARE(simple.gaugeProperties(route).value("closeUp").toDouble(), 600.0);
        QVERIFY(!simple.gaugeProperties(route).value("bySpeed").toBool());
        // by speed: the second speed above the first, or nothing changes
        QVERIFY(!simple.setGaugeCloseUp(route, 300, 2000, 40, 10));
        QCOMPARE(simple.gaugeProperties(route).value("closeUp").toDouble(), 600.0);
        QVERIFY(simple.setGaugeCloseUp(route, 300, 2000, 10, 40));
        const QVariantMap bySpeed = simple.gaugeProperties(route);
        QVERIFY(bySpeed.value("bySpeed").toBool());
        QCOMPARE(bySpeed.value("far").toDouble(), 2000.0);
        QCOMPARE(bySpeed.value("slow").toDouble(), 10.0);
        QCOMPARE(bySpeed.value("fast").toDouble(), 40.0);
        QVERIFY(simple.setGaugeCloseUp(route, 0, 0, 0, 0));
        QVERIFY(!simple.gaugeProperties(route).value("bySpeed").toBool());
        simple.setGaugeHeadingUp(route, true);
        QCOMPARE(simple.project().overlay.at(route).headingUp, 1);
        simple.setGaugeStreets(route, false);
        QVERIFY(!simple.gaugeProperties(route).value("streets").toBool());
        simple.setGaugeFlyover(route, true);
        QVERIFY(simple.gaugeProperties(route).value("flyover").toBool());
        simple.setGaugeFlyoverCamera(route, 45, 300, 9);
        const QVariantMap camera = simple.gaugeProperties(route);
        QCOMPARE(camera.value("tilt").toDouble(), 45.0);
        QCOMPARE(camera.value("cameraHeight").toDouble(), 300.0);
        QCOMPARE(camera.value("hills").toDouble(), 5.0);
        simple.setGaugeFlyoverCamera(route, 0, 0, 0);
        QVERIFY(!simple.project().overlay.at(route).flyoverCamera.isSet());
        simple.setGaugeGhost(route, true);
        QCOMPARE(simple.project().overlay.at(route).ghost, 1);
        QVERIFY(simple.gaugeProperties(route).value("mapIds").toStringList().first().isEmpty());
    }

    // No video: the demo ride, its route in 3D the whole picture; a video put in place of it.
    void demoFlyover()
    {
        SimpleController simple(false);
        simple.openDemo();
        QVERIFY(simple.isDemo());
        QVERIFY(simple.hasData());
        QVERIFY(simple.dataDuration() > 60000);
        QVERIFY(!simple.canSave());
        QCOMPARE(simple.gaugeCount(), 1);
        QVERIFY(qAbs(simple.aspect() - 16.0 / 9.0) < 1e-9);
        const QVariantMap flyover = simple.gaugeProperties(0);
        QVERIFY(flyover.value("flyover").toBool());
        QVERIFY(flyover.value("headingUp").toBool());
        QCOMPARE(flyover.value("closeUp").toDouble(), 300.0);
        // its map the whole picture: the gauge a little more
        QVERIFY(flyover.value("width").toDouble() >= 1.0);
        // a look at it, when asked for
        if (!qEnvironmentVariableIsEmpty("SIMPLE_FLYOVER_PNG"))
            {
                QImage picture(960, 540, QImage::Format_ARGB32_Premultiplied);
                picture.fill(Qt::black);
                QPainter painter(&picture);
                simple.overlay()->render(painter, QRectF(0, 0, 960, 540), 205.0);
                painter.end();
                picture.save(qEnvironmentVariable("SIMPLE_FLYOVER_PNG"));
            }
        // more than the picture, it is moved about covering it: never in from the edges
        const QRectF whole = simple.gaugeRect(0, 960, 540);
        QVERIFY(whole.x() < 0.0 && whole.y() < 0.0);
        simple.moveGauge(0, whole.x() / 960.0 + 0.01, whole.y() / 540.0, true);
        QVERIFY(qAbs(simple.gaugeRect(0, 960, 540).x() - (whole.x() + 9.6)) < 0.5);
        simple.moveGauge(0, 0.3, 0.3, true);
        QRectF moved = simple.gaugeRect(0, 960, 540);
        QVERIFY(qAbs(moved.x()) < 1e-6 && qAbs(moved.y()) < 1e-6);
        simple.moveGauge(0, -5.0, -5.0, true);
        moved = simple.gaugeRect(0, 960, 540);
        QVERIFY(moved.right() >= 960.0 - 0.5 && moved.bottom() >= 540.0 - 0.5);
        simple.moveGauge(0, whole.x() / 960.0, whole.y() / 540.0, true);

        // its laps, the line put where the rider is
        QVERIFY(simple.lineHere(20000));
        QVERIFY(simple.hasLine());
        QVERIFY(!simple.laps().isEmpty());
        const QVariantMap first = simple.laps().first().toMap();
        QCOMPARE(first.value("number").toInt(), 1);
        QVERIFY(!first.value("time").toString().isEmpty());
        simple.setReferenceLap(1);
        QVERIFY(simple.laps().first().toMap().value("reference").toBool());
        simple.clearLine();
        QVERIFY(!simple.hasLine());

        // a dash over it: the route in 3D stays under its gauges
        if (!simple.templateNames().isEmpty())
            {
                simple.chooseTemplate(0);
                QVERIFY(simple.gaugeCount() > 1);
                QVERIFY(simple.gaugeProperties(0).value("flyover").toBool());
                QVERIFY(!simple.gaugeProperties(1).value("flyover").toBool());
                if (!qEnvironmentVariableIsEmpty("SIMPLE_DASH_PNG"))
                    {
                        QImage picture(960, 540, QImage::Format_ARGB32_Premultiplied);
                        picture.fill(Qt::black);
                        QPainter painter(&picture);
                        simple.overlay()->render(painter, QRectF(0, 0, 960, 540), 205.0);
                        painter.end();
                        picture.save(qEnvironmentVariable("SIMPLE_DASH_PNG"));
                    }
            }

        if (m_video.isEmpty())
            {
                return;
            }
        // one's own video: the demo is gone, the dash chosen over the video without the 3D under it
        const int dash = simple.gaugeCount() - 1;
        simple.openVideo(QUrl::fromLocalFile(m_video));
        // a video without data of its own: the demo stays over it, no complaint, not out of step
        QTRY_VERIFY_WITH_TIMEOUT(!simple.busy() && simple.aspect() < 1.5, 20000);
        QVERIFY(simple.isDemo());
        QVERIFY(simple.hasData());
        QVERIFY(simple.message().isEmpty());
        QCOMPARE(simple.syncOffset(), 0.0);
        if (!simple.templateNames().isEmpty())
            {
                QCOMPARE(simple.gaugeCount(), dash);
            }
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(!simple.isDemo() && simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.dataName(), QString("ride.gpx"));
        QVERIFY(simple.gaugeCount() > 0);
        if (!simple.templateNames().isEmpty())
            {
                QCOMPARE(simple.gaugeCount(), dash);
            }
        for (int i = 0; i < simple.gaugeCount(); ++i)
            {
                QVERIFY(simple.gaugeProperties(i).value("width").toDouble() <= 1.0 + 1e-9);
            }
    }

    // Data without a video: the flyover, as long as the data, then a video and a dash.
    void dataFirst()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.gaugeCount(), 1);
        QVERIFY(simple.gaugeProperties(0).value("flyover").toBool());
        QCOMPARE(simple.dataDuration(), qint64(10000));
        QCOMPARE(simple.syncOffset(), 0.0);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        QTRY_VERIFY_WITH_TIMEOUT(!simple.busy() && simple.aspect() < 1.5, 20000);
        QVERIFY(!simple.gaugeProperties(0).value("flyover").toBool() || simple.templateNames().isEmpty());
    }

    // One Open for everything, Save, New; a project opened again as it was saved.
    void openSaveNew()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString song = m_dir.filePath("open.wav");
        QVERIFY(FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                            "sine=frequency=440:duration=3", song}).exitCode == 0);
        SimpleController simple(false);
        QVERIFY(!simple.modified());
        simple.openFiles({QUrl::fromLocalFile(m_data), QUrl::fromLocalFile(song), QUrl::fromLocalFile(m_video)});
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.videoName(), QString("ride.mp4"));
        QCOMPARE(simple.dataName(), QString("ride.gpx"));
        QCOMPARE(simple.musicNames(), QStringList{"open.wav"});
        QVERIFY(simple.modified());
        QVERIFY(simple.canSave());
        QVERIFY(!simple.save());   // no file yet
        simple.setSyncOffset(1.5);
        simple.addGauge("builtin:speedometer");
        const int gauges = simple.gaugeCount();
        QVERIFY(simple.saveAs(QUrl::fromLocalFile(m_dir.filePath("saved"))));
        QVERIFY(!simple.modified());
        QCOMPARE(simple.projectName(), QString("saved"));
        const QString saved = m_dir.filePath("saved.rtvproj");
        QVERIFY(QFileInfo::exists(saved));

        simple.newProject();
        QVERIFY(!simple.hasData());
        QVERIFY(simple.videoName().isEmpty());
        QVERIFY(simple.musicNames().isEmpty());
        QVERIFY(simple.projectName().isEmpty());

        simple.openFiles({QUrl::fromLocalFile(saved)});
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        QCOMPARE(simple.gaugeCount(), gauges);
        QCOMPARE(simple.syncOffset(), 1.5);
        QCOMPARE(simple.musicNames(), QStringList{"open.wav"});
        QCOMPARE(simple.projectName(), QString("saved"));
        QVERIFY(!simple.modified());
    }

    // A corner dragged: a gauge that keeps its shape takes the height, a free one both.
    void corners()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        SimpleController simple(false);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        simple.openData(QUrl::fromLocalFile(m_data));
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        simple.addGauge("builtin:speedometer");
        const int dial = simple.gaugeCount() - 1;
        const QRectF before = simple.gaugeRect(dial, 320, 240);
        simple.setGaugeRect(dial, 0.1, 0.1, 0.9, 0.5, true);
        QRectF now = simple.gaugeRect(dial, 320, 240);
        QVERIFY(qAbs(now.height() - 120.0) < 0.5);
        // its own shape: the width follows
        QVERIFY(qAbs(now.width() / now.height() - before.width() / before.height()) < 1e-6);
        simple.setGaugeKeepShape(dial, false);
        simple.setGaugeRect(dial, 0.1, 0.1, 0.6, 0.3, true);
        now = simple.gaugeRect(dial, 320, 240);
        QVERIFY(qAbs(now.width() - 192.0) < 0.5);
        QVERIFY(qAbs(now.height() - 72.0) < 0.5);
        QVERIFY(qAbs(now.x() - 32.0) < 0.5);
    }

    // The laps map and the graph under the video, painted on their own: something drawn, a tap
    // on the map put on the route, recent files, colors and a part of the video exported.
    void drawnAndTheRest()
    {
        SimpleController simple(false);
        simple.openDemo();
        QVERIFY(simple.channelIds().contains("speed"));
        QCOMPARE(simple.channelIds().size(), simple.channelNames().size());
        QVERIFY(simple.lineHere(20000));

        RouteView map;
        map.setSize(QSizeF(400, 300));
        map.setStreets(false);
        map.setController(&simple);
        map.setHighlightLap(1);
        map.setPosition(100000);
        QImage mapPicture(400, 300, QImage::Format_ARGB32_Premultiplied);
        mapPicture.fill(Qt::transparent);
        {
            QPainter painter(&mapPicture);
            map.paint(&painter);
        }
        QVERIFY(mapPicture.pixelColor(200, 150) != QColor() );
        // red of the lap drawn somewhere
        bool red = false;
        for (int y = 0; y < 300 && !red; y += 2)
            {
                for (int x = 0; x < 400 && !red; x += 2)
                    {
                        const QColor c = mapPicture.pixelColor(x, y);
                        red = c.red() > 180 && c.green() < 90 && c.blue() < 90;
                    }
            }
        QVERIFY(red);
        // where the rider is on the map: the line put there
        simple.clearLine();
        double lat = 0.0;
        double lon = 0.0;
        {
            const TelemetrySessionPtr session = simple.session();
            lat = session->channel(ChannelId::PositionLat)->valueAt(100.0);
            lon = session->channel(ChannelId::PositionLon)->valueAt(100.0);
        }
        QVERIFY(simple.placeLine(lat, lon));
        QVERIFY(simple.hasLine());
        QVERIFY(!simple.placeLine(lat + 1.0, lon));   // far off the route

        ChannelGraph graph;
        graph.setSize(QSizeF(600, 64));
        graph.setController(&simple);
        graph.setChannel("speed");
        graph.setDuration(simple.dataDuration());
        graph.setPosition(simple.dataDuration() / 2);
        QImage graphPicture(600, 64, QImage::Format_ARGB32_Premultiplied);
        graphPicture.fill(Qt::black);
        {
            QPainter painter(&graphPicture);
            graph.paint(&painter);
        }
        int lit = 0;
        for (int x = 0; x < 600; ++x)
            {
                for (int y = 0; y < 64; ++y)
                    {
                        lit += graphPicture.pixelColor(x, y).blue() > 150 ? 1 : 0;
                    }
            }
        QVERIFY(lit > 300);
        // painted again further on: the line comes from what was kept, only the cursor moves
        graph.setPosition(simple.dataDuration() / 4);
        QImage again(600, 64, QImage::Format_ARGB32_Premultiplied);
        again.fill(Qt::black);
        {
            QPainter painter(&again);
            graph.paint(&painter);
        }
        int litAgain = 0;
        for (int x = 0; x < 600; ++x)
            {
                for (int y = 0; y < 64; ++y)
                    {
                        litAgain += again.pixelColor(x, y).blue() > 150 ? 1 : 0;
                    }
            }
        QVERIFY(std::abs(litAgain - lit) < lit / 10);
        // the cursor (amber: much red, little blue) at a quarter now, not at the middle
        auto amber = [](const QImage &picture, int x)
        {
            const QColor c = picture.pixelColor(x, 40);
            return c.red() > 200 && c.blue() < 100;
        };
        QVERIFY(amber(again, 150) && !amber(again, 300));
        QVERIFY(amber(graphPicture, 300));
        if (!qEnvironmentVariableIsEmpty("SIMPLE_DRAWN_PNG"))
            {
                mapPicture.save(qEnvironmentVariable("SIMPLE_DRAWN_PNG") + "-map.png");
                graphPicture.save(qEnvironmentVariable("SIMPLE_DRAWN_PNG") + "-graph.png");
            }

        // colors: of all gauges, then of one alone
        const QColor red50(255, 0, 0, 128);
        simple.setGaugeColor(0, 2, red50);
        QCOMPARE(simple.project().theme.accent, red50);
        QVERIFY(!simple.gaugeProperties(0).value("ownColors").toBool());
        simple.setGaugeOwnColors(0, true);
        simple.setGaugeColor(0, 0, Qt::green);
        QCOMPARE(simple.project().overlay.first().theme.foreground, QColor(Qt::green));
        QCOMPARE(simple.project().theme.foreground, GaugeTheme().foreground);
        QCOMPARE(simple.gaugeProperties(0).value("colors").toList().at(0).value<QColor>(), QColor(Qt::green));

        if (m_video.isEmpty())
            {
                return;
            }
        // opened: among the recent ones
        simple.openFiles({QUrl::fromLocalFile(m_video), QUrl::fromLocalFile(m_data)});
        QTRY_VERIFY_WITH_TIMEOUT(!simple.isDemo() && simple.hasData() && !simple.busy(), 20000);
        bool video = false;
        for (const QVariant &file : simple.recentFiles())
            {
                video = video || file.toMap().value("name").toString() == "ride.mp4";
            }
        QVERIFY(video);
        // a part of it exported
        simple.startExportRange(0.5, 1.5);
        QTRY_VERIFY_WITH_TIMEOUT(simple.exporting(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(!simple.exporting(), 120000);
        QVERIFY2(simple.exportedUrl().isValid(), qPrintable(simple.exportText()));
        MediaInfo part;
        QString error;
        QVERIFY2(MediaProbe::probe(FfmpegTools::ffprobeProgram(), simple.exportedUrl().toLocalFile(), &part, &error, false),
                 qPrintable(error));
        QVERIFY(part.duration > 0.8 && part.duration < 1.3);
        simple.startExportRange(1.5, 0.5);
        QVERIFY(!simple.exporting());
    }

    // Two videos opened together: joining them offered, done by copying, the joined one opened.
    void joinVideos()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString second = m_dir.filePath("ride2.mp4");
        QVERIFY(QFile::copy(m_video, second));
        SimpleController simple(false);
        QSignalSpy offered(&simple, &SimpleController::joinOffered);
        simple.openFiles({QUrl::fromLocalFile(second), QUrl::fromLocalFile(m_video)});
        QCOMPARE(offered.count(), 1);
        QVERIFY(simple.videoName().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(simple.joinReady(), 20000);
        QCOMPARE(simple.joinNames(), (QStringList{"ride.mp4", "ride2.mp4"}));
        QVERIFY(!simple.joinText().isEmpty());
        // put the other way round by hand: joined in that order, named after the first
        simple.moveJoinPart(1, -1);
        QCOMPARE(simple.joinNames(), (QStringList{"ride2.mp4", "ride.mp4"}));
        QVERIFY(simple.joinText().contains("ride2_joined"));
        simple.moveJoinPart(0, -1);   // nowhere to go
        QCOMPARE(simple.joinNames().first(), QString("ride2.mp4"));
        simple.startJoin();
        QVERIFY(simple.joining());
        QTRY_VERIFY_WITH_TIMEOUT(!simple.joining() && simple.videoName().startsWith("ride2_joined"), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(!simple.busy(), 20000);
        QVERIFY(simple.joinNames().isEmpty());
        QVERIFY(QFileInfo::exists(m_dir.filePath(simple.videoName())));

        // cancelled before it begins: nothing opened
        SimpleController other(false);
        other.openFiles({QUrl::fromLocalFile(second), QUrl::fromLocalFile(m_video)});
        other.cancelJoin();
        QVERIFY(other.joinNames().isEmpty());
        QVERIFY(other.videoName().isEmpty());
    }

    // Only the gauges, on a see-through picture: a .webm beside the video.
    void gaugesOnlyExport()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        if (!FfmpegTools::run("ffmpeg", {"-hide_banner", "-encoders"}).output.contains("libvpx-vp9"))
            {
                QSKIP("ffmpeg has no VP9 encoder");
            }
        SimpleController simple(false);
        simple.openFiles({QUrl::fromLocalFile(m_video), QUrl::fromLocalFile(m_data)});
        QTRY_VERIFY_WITH_TIMEOUT(simple.hasData() && !simple.busy(), 20000);
        simple.setGaugesOnly(true);
        simple.startExport();
        QTRY_VERIFY_WITH_TIMEOUT(simple.exporting(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(!simple.exporting(), 120000);
        QVERIFY2(simple.exportedUrl().isValid(), qPrintable(simple.exportText()));
        QVERIFY(simple.exportedUrl().toLocalFile().endsWith("_gauges.webm"));
    }

    // Music under the video: what plays when, and how loud.
    void music()
    {
        if (m_video.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString song = m_dir.filePath("song.wav");
        QVERIFY(FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                            "sine=frequency=440:duration=5", song}).exitCode == 0);
        SimpleController simple(false);
        simple.openVideo(QUrl::fromLocalFile(m_video));
        QVERIFY(simple.musicAt(500).value("url").toUrl().isEmpty());
        simple.addMusic(QUrl::fromLocalFile(song));
        QCOMPARE(simple.musicNames(), QStringList{"song.wav"});
        const QVariantMap now = simple.musicAt(1500);
        QCOMPARE(now.value("url").toUrl(), QUrl::fromLocalFile(song));
        QVERIFY(qAbs(now.value("position").toLongLong() - 1500) < 5);
        simple.setMusicVolume(50);
        simple.setVideoVolume(500);
        QCOMPARE(simple.videoVolume(), 200);
        simple.removeMusic(0);
        QVERIFY(simple.musicNames().isEmpty());
    }
};

QTEST_MAIN(TestSimple)
#include "test_simple.moc"
