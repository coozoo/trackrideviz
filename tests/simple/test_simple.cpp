#include "simple/simplecontroller.h"
#include "gauge/gaugelibrary.h"
#include "overlay/templatelibrary.h"
#include "video/ffmpegtools.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
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
