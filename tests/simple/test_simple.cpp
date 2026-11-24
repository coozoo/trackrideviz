#include "simple/simplecontroller.h"
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
};

QTEST_MAIN(TestSimple)
#include "test_simple.moc"
