#include "gauge/builtingauges.h"
#include "overlay/overlay.h"
#include "telemetry/channelids.h"
#include "video/exportjob.h"
#include "video/exportplan.h"
#include "video/ffmpegtools.h"
#include "video/mediainfo.h"
#include "video/videoencoders.h"

#include <QDir>
#include <QHash>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestExport : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_sourceVideo;   // made by ffmpeg in initTestCase; empty when ffmpeg is missing

    static MediaInfo fullHd60()
    {
        MediaInfo info;
        info.size = QSize(1920, 1080);
        info.fpsNum = 60;
        info.fpsDen = 1;
        info.duration = 100.0;
        info.hasAudio = true;
        return info;
    }

    // The dial in the lower right quarter, showing a steady 5 m/s.
    static Overlay oneGauge()
    {
        OverlayItem item;
        item.gaugeId = BuiltinGauges::DialId;
        item.x = 0.5;
        item.y = 0.5;
        item.height = 0.25;
        return Overlay({item}, steadySession(), Units(), GaugeTheme());
    }

    static ExportSettings settingsFor(const QString &output)
    {
        ExportSettings settings;
        settings.outputPath = output;
        settings.rateControl = ExportSettings::Quality;
        settings.quality = 20;
        return settings;
    }

    // Value following `option`, e.g. the graph after -filter_complex.
    static QString valueOf(const QStringList &args, const QString &option)
    {
        const int index = args.indexOf(option);
        return index >= 0 ? args.value(index + 1) : QString();
    }

    static bool hasPair(const QStringList &args, const QString &option, const QString &value)
    {
        for (int i = 0; i + 1 < args.size(); ++i)
            {
                if (args.at(i) == option && args.at(i + 1) == value)
                    {
                        return true;
                    }
            }
        return false;
    }

    static TelemetrySessionPtr steadySession()
    {
        auto session = std::make_shared<TelemetrySession>();
        Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
        speed.append(0.0, 5.0);
        speed.append(100.0, 5.0);
        return session;
    }

    // One frame of a video as an image, through ffmpeg.
    QImage frameAt(const QString &video, double time)
    {
        const QString png = m_dir.filePath("frame.png");
        QFile::remove(png);
        FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-ss", QString::number(time),
                                    "-i", video, "-frames:v", "1", png});
        return QImage(png);
    }

    static int countFrames(const QString &video)
    {
        const FfmpegTools::RunResult result = FfmpegTools::run("ffprobe",
        {"-v", "error", "-count_frames", "-select_streams", "v", "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", video}, 60000);
        return result.output.trimmed().toInt();
    }

    // Seconds of each stream type: "video" -> 3.0, "audio" -> 3.0
    static QHash<QString, double> streamDurations(const QString &video)
    {
        const FfmpegTools::RunResult result = FfmpegTools::run("ffprobe",
        {"-v", "error", "-show_entries", "stream=codec_type,duration", "-of", "csv=p=0", video});
        QHash<QString, double> durations;
        for (const QByteArray &line : result.output.split('\n'))
            {
                const QList<QByteArray> parts = line.trimmed().split(',');
                if (parts.size() == 2)
                    {
                        durations.insert(QString::fromLatin1(parts.at(0)), parts.at(1).toDouble());
                    }
            }
        return durations;
    }

    // Ten minutes of 720p, with or without a tone: long enough that an export cannot finish by itself in a test.
    QString longSource(bool withSound)
    {
        const QString path = m_dir.filePath(withSound ? "long-sound.mp4" : "long-silent.mp4");
        if (QFile::exists(path))
            {
                return path;
            }
        QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "color=c=gray:s=1280x720:r=30:d=600"};
        if (withSound)
            {
                args << "-f" << "lavfi" << "-i" << "sine=frequency=440:duration=600" << "-c:a" << "aac";
            }
        args << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-g" << "300" << path;
        return FfmpegTools::run("ffmpeg", args, 180000).exitCode == 0 ? path : QString();
    }

    static int difference(const QImage &a, const QImage &b, int x, int y)
    {
        const QColor ca = a.pixelColor(x, y);
        const QColor cb = b.pixelColor(x, y);
        return qAbs(ca.red() - cb.red()) + qAbs(ca.green() - cb.green()) + qAbs(ca.blue() - cb.blue());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        if (FfmpegTools::versionLine("ffmpeg").isEmpty())
            {
                return;
            }
        // 2 s, 320x240, 30 fps, flat grey, with a tone
        const QString path = m_dir.filePath("source.mp4");
        const FfmpegTools::RunResult made = FfmpegTools::run("ffmpeg",
        {
            "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "color=c=gray:s=320x240:r=30:d=2",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", path
        }, 60000);
        if (made.started && made.exitCode == 0)
            {
                m_sourceVideo = path;
            }
    }

    // --- sizes ------------------------------------------------------------

    void outputSize_data()
    {
        QTest::addColumn<QSize>("source");
        QTest::addColumn<int>("wanted");
        QTest::addColumn<QSize>("expected");
        QTest::newRow("keep") << QSize(1920, 1080) << 0 << QSize(1920, 1080);
        QTest::newRow("720p") << QSize(1920, 1080) << 720 << QSize(1280, 720);
        QTest::newRow("never upscale") << QSize(1280, 720) << 1080 << QSize(1280, 720);
        QTest::newRow("4:3 to 720") << QSize(2704, 2028) << 720 << QSize(960, 720);
        QTest::newRow("odd source made even") << QSize(853, 481) << 0 << QSize(854, 482);
        QTest::newRow("odd result made even") << QSize(1920, 800) << 360 << QSize(864, 360);
        QTest::newRow("portrait") << QSize(1080, 1920) << 1280 << QSize(720, 1280);
    }

    void outputSize()
    {
        QFETCH(QSize, source);
        QFETCH(int, wanted);
        QFETCH(QSize, expected);
        QCOMPARE(ExportPlan::outputSizeFor(source, wanted), expected);
    }

    // --- command line -----------------------------------------------------

    void planPipesOnlyTheGaugeArea()
    {
        const ExportPlan plan = ExportPlan::make("/v/in.mp4", fullHd60(), settingsFor("/v/out.mp4"), oneGauge());
        QCOMPARE(plan.outputSize, QSize(1920, 1080));
        QCOMPARE(plan.overlayRect, QRect(960, 540, 270, 270));
        QCOMPARE(plan.frameCount, qint64(6000));
        QCOMPARE(plan.frameTime(0), 0.0);
        QCOMPARE(plan.frameTime(600), 10.0);

        const QStringList &args = plan.arguments;
        QVERIFY(hasPair(args, "-i", "/v/in.mp4"));
        QVERIFY(hasPair(args, "-i", "pipe:0"));
        QVERIFY(hasPair(args, "-video_size", "270x270"));
        QVERIFY(hasPair(args, "-framerate", "60/1"));
        QVERIFY(hasPair(args, "-pixel_format", QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "bgra" : "argb"));
        QCOMPARE(valueOf(args, "-filter_complex"),
                 QString("[0:v][1:v]overlay=960:540:alpha=premultiplied:eof_action=endall,format=yuv420p[v]"));
        QVERIFY(hasPair(args, "-map", "[v]"));
        QVERIFY(hasPair(args, "-c:v", "libx264"));
        QVERIFY(hasPair(args, "-crf", "20"));
        QVERIFY(hasPair(args, "-preset", "medium"));
        QVERIFY(hasPair(args, "-movflags", "+faststart"));
        QCOMPARE(args.last(), QString("/v/out.mp4"));
        // the video comes before the pipe: the filter graph relies on that order
        QVERIFY(args.indexOf("/v/in.mp4") < args.indexOf("pipe:0"));
    }

    void planScalesBeforeTheOverlay()
    {
        ExportSettings settings = settingsFor("/v/out.mp4");
        settings.height = 720;
        const ExportPlan plan = ExportPlan::make("/v/in.mp4", fullHd60(), settings, oneGauge());
        QCOMPARE(plan.outputSize, QSize(1280, 720));
        // the gauge keeps its place on the smaller frame
        QCOMPARE(plan.overlayRect, QRect(640, 360, 180, 180));
        QCOMPARE(valueOf(plan.arguments, "-filter_complex"),
                 QString("[0:v]scale=1280:720[base];[base][1:v]overlay=640:360:alpha=premultiplied:eof_action=endall,format=yuv420p[v]"));
    }

    void planKeepsExactFrameRate()
    {
        MediaInfo info = fullHd60();
        info.fpsNum = 60000;
        info.fpsDen = 1001;
        info.duration = 10.0;
        const ExportPlan plan = ExportPlan::make("in.mp4", info, settingsFor("out.mp4"), oneGauge());
        QVERIFY(hasPair(plan.arguments, "-framerate", "60000/1001"));
        QCOMPARE(plan.frameCount, qint64(599)); // 599.4 rounded down: never one too many
        QVERIFY(qAbs(plan.frameTime(60000) - 1001.0) < 1e-9);
    }

    void planWithRange()
    {
        ExportSettings settings = settingsFor("out.mp4");
        settings.start = 30.0;
        settings.duration = 12.5;
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        const QStringList &args = plan.arguments;
        QVERIFY(hasPair(args, "-ss", "30.000"));
        QVERIFY(hasPair(args, "-t", "12.500"));
        // input options: they must stand before the video they apply to
        QVERIFY(args.indexOf("-ss") < args.indexOf("in.mp4"));
        QVERIFY(args.indexOf("-t") < args.indexOf("in.mp4"));
        QCOMPARE(plan.frameCount, qint64(750));
        // the first picture belongs to second 30 of the video
        QCOMPARE(plan.frameTime(0), 30.0);
        QCOMPARE(plan.frameTime(60), 31.0);
    }

    void planRangeIsLimitedToTheVideo()
    {
        ExportSettings settings = settingsFor("out.mp4");
        settings.start = 95.0;
        settings.duration = 60.0;
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-t", "5.000"));
        QCOMPARE(plan.frameCount, qint64(300));

        settings.start = 500.0; // past the end
        const ExportPlan past = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QCOMPARE(past.startTime, 100.0);
        QCOMPARE(past.frameCount, qint64(1));
    }

    void planUsesExactFrameCountWhenKnown()
    {
        MediaInfo info = fullHd60();
        info.frameCount = 5997; // a few frames short of duration x rate, as real files are
        const ExportPlan whole = ExportPlan::make("in.mp4", info, settingsFor("out.mp4"), oneGauge());
        QCOMPARE(whole.frameCount, qint64(5997));

        // of a part only the estimate is available
        ExportSettings settings = settingsFor("out.mp4");
        settings.start = 10.0;
        settings.duration = 5.0;
        QCOMPARE(ExportPlan::make("in.mp4", info, settings, oneGauge()).frameCount, qint64(300));
    }

    void planRecordsWhatARewriteNeeds()
    {
        ExportSettings settings = settingsFor("/v/out.mp4");
        ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QCOMPARE(plan.outputPath, QString("/v/out.mp4"));
        QVERIFY(plan.hasAudio);
        QCOMPARE(plan.muxerArguments, QStringList({"-movflags", "+faststart"}));
        QCOMPARE(plan.lengthOf(90), 1.5);

        settings.audio = ExportSettings::NoAudio;
        QVERIFY(!ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge()).hasAudio);
        MediaInfo silent = fullHd60();
        silent.hasAudio = false;
        QVERIFY(!ExportPlan::make("in.mp4", silent, settingsFor("out.mkv"), oneGauge()).hasAudio);
    }

    void planWholeVideoHasNoRangeOptions()
    {
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor("out.mp4"), oneGauge());
        QVERIFY(!plan.arguments.contains("-ss"));
        QVERIFY(!plan.arguments.contains("-t"));
    }

    void planPipesSeparateRegionsStacked()
    {
        OverlayItem topLeft;
        topLeft.gaugeId = BuiltinGauges::DialId;
        topLeft.height = 0.25;
        OverlayItem bottomRight = topLeft;
        bottomRight.x = 0.75;
        bottomRight.y = 0.75;
        const Overlay overlay({topLeft, bottomRight}, steadySession(), Units(), GaugeTheme());
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor("out.mp4"), overlay);
        QCOMPARE(plan.regions, QVector<QRect>({QRect(0, 0, 270, 270), QRect(1440, 810, 270, 270)}));
        QCOMPARE(plan.stackTops, QVector<int>({0, 270}));
        QCOMPARE(plan.stackSize, QSize(270, 540));
        QVERIFY(hasPair(plan.arguments, "-video_size", "270x540"));
        QCOMPARE(valueOf(plan.arguments, "-filter_complex"),
                 QString("[1:v]split=2[s0][s1];[s0]crop=270:270:0:0[g0];[s1]crop=270:270:0:270[g1];"
                         "[0:v][g0]overlay=0:0:alpha=premultiplied:eof_action=endall[o0];"
                         "[o0][g1]overlay=1440:810:alpha=premultiplied:eof_action=endall,format=yuv420p[v]"));
        // the whole frame is no longer sent
        QCOMPARE(plan.overlayRect, QRect(0, 0, 1710, 1080));
    }

    void planWithoutGaugesHasNoPipe()
    {
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor("out.mp4"), Overlay());
        QVERIFY(plan.overlayRect.isEmpty());
        QCOMPARE(plan.frameCount, qint64(0));
        QVERIFY(!plan.arguments.contains("pipe:0"));
        QCOMPARE(valueOf(plan.arguments, "-filter_complex"), QString("[0:v]format=yuv420p[v]"));
    }

    void planAudioModes()
    {
        ExportSettings settings = settingsFor("out.mp4");
        ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-map", "0:a?"));
        QVERIFY(hasPair(plan.arguments, "-c:a", "copy"));

        settings.audio = ExportSettings::AacAudio;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-c:a", "aac"));

        settings.audio = ExportSettings::NoAudio;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(plan.arguments.contains("-an"));
        QVERIFY(!plan.arguments.contains("0:a?"));
    }

    void planContainerSpecifics()
    {
        ExportSettings settings = settingsFor("out.mkv");
        settings.encoder = "libx265";
        ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(!plan.arguments.contains("-movflags"));
        QVERIFY(!plan.arguments.contains("-tag:v"));

        settings.outputPath = "out.MP4";
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-tag:v", "hvc1"));
        QVERIFY(hasPair(plan.arguments, "-movflags", "+faststart"));
    }

    void planForVaapi()
    {
        ExportSettings settings = settingsFor("out.mp4");
        settings.encoder = "h264_vaapi";
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        const QStringList &args = plan.arguments;
        QVERIFY(args.contains("-vaapi_device"));
        // the device has to be known before the inputs are opened
        QVERIFY(args.indexOf("-vaapi_device") < args.indexOf("-i"));
        QVERIFY(valueOf(args, "-filter_complex").endsWith(",format=nv12,hwupload[v]"));
        QVERIFY(hasPair(args, "-rc_mode", "CQP"));
        QVERIFY(hasPair(args, "-qp", "20"));
        QVERIFY(!args.contains("-preset"));
    }

    // --- encoders -----------------------------------------------------------

    void encoderRateControl()
    {
        ExportSettings settings;
        settings.rateControl = ExportSettings::Quality;
        settings.quality = 23;
        settings.bitrateKbps = 12000;

        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("libx264"), settings),
                 QStringList({"-c:v", "libx264", "-crf", "23", "-preset", "medium"}));
        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("h264_nvenc"), settings),
                 QStringList({"-c:v", "h264_nvenc", "-rc", "vbr", "-cq", "23", "-b:v", "0"}));
        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("hevc_qsv"), settings),
                 QStringList({"-c:v", "hevc_qsv", "-global_quality", "23"}));

        settings.rateControl = ExportSettings::Bitrate;
        settings.speed = ExportSettings::Fast;
        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("libx265"), settings),
                 QStringList({"-c:v", "libx265", "-b:v", "12000k", "-preset", "veryfast"}));
        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("hevc_vaapi"), settings),
                 QStringList({"-c:v", "hevc_vaapi", "-rc_mode", "VBR", "-b:v", "12000k"}));
    }

    void encoderWithoutQualityModeUsesBitrate()
    {
        ExportSettings settings;
        settings.rateControl = ExportSettings::Quality;
        settings.bitrateKbps = 8000;
        QCOMPARE(VideoEncoders::arguments(VideoEncoders::byId("h264_videotoolbox"), settings),
                 QStringList({"-c:v", "h264_videotoolbox", "-b:v", "8000k"}));
    }

    void unknownEncoderFallsBackToX264()
    {
        QCOMPARE(VideoEncoders::byId("no_such_encoder").id, QString("libx264"));
        QCOMPARE(VideoEncoders::byId(QString()).id, QString("libx264"));
    }

    void knownEncodersAreConsistent()
    {
        QSet<QString> ids;
        for (const VideoEncoder &encoder : VideoEncoders::known())
            {
                QVERIFY2(!ids.contains(encoder.id), qPrintable(encoder.id));
                ids.insert(encoder.id);
                QVERIFY(!encoder.label.isEmpty());
                QCOMPARE(encoder.hevc, encoder.id.contains("hevc") || encoder.id.contains("265"));
                QVERIFY(!VideoEncoders::outputFilter(encoder).isEmpty());
                QCOMPARE(VideoEncoders::arguments(encoder, ExportSettings()).value(1), encoder.id);
            }
    }

    // --- tools --------------------------------------------------------------

    void ffprobeIsFoundNextToFfmpeg()
    {
        QCOMPARE(FfmpegTools::ffprobeFor("ffmpeg"), QString("ffprobe"));
        QCOMPARE(FfmpegTools::ffprobeFor("/opt/ffmpeg-8/bin/ffmpeg"), QString("/opt/ffmpeg-8/bin/ffprobe"));
        QCOMPARE(FfmpegTools::ffprobeFor("/tools/ffmpeg.exe"), QString("/tools/ffprobe.exe"));
    }

    void missingProgramIsReported()
    {
        const FfmpegTools::RunResult result = FfmpegTools::run("/no/such/dir/ffmpeg", {"-version"});
        QVERIFY(!result.started);
        QVERIFY(FfmpegTools::versionLine("/no/such/dir/ffmpeg").isEmpty());
    }

    // --- ffprobe output -----------------------------------------------------

    void probeParsesActionCameraFile()
    {
        const QByteArray json = R"({
            "streams": [
                {"index": 0, "codec_name": "h264", "codec_type": "video", "width": 1920, "height": 1080,
                 "r_frame_rate": "60/1", "avg_frame_rate": "64794000/1080157", "duration": "1080.157000",
                 "bit_rate": "26574007",
                 "disposition": {"attached_pic": 0}},
                {"index": 1, "codec_name": "aac", "codec_type": "audio", "duration": "1080.159000"}
            ],
            "format": {"duration": "1080.159000"}
        })";
        MediaInfo info;
        QString error;
        QVERIFY2(MediaProbe::parse(json, &info, &error), qPrintable(error));
        QCOMPARE(info.size, QSize(1920, 1080));
        QCOMPARE(info.fpsNum, 64794000);
        QCOMPARE(info.fpsDen, 1080157);
        QVERIFY(qAbs(info.fps() - 59.99) < 0.01);
        QCOMPARE(info.duration, 1080.157);
        QCOMPARE(info.bitrateKbps, 26574);
        QCOMPARE(info.videoCodec, QString("h264"));
        QVERIFY(info.hasAudio);
        QCOMPARE(info.audioCodec, QString("aac"));
    }

    void probeAppliesRotation()
    {
        MediaInfo info;
        QVERIFY(MediaProbe::parse(R"({"streams": [{"codec_type": "video", "width": 1920, "height": 1080,
            "avg_frame_rate": "30/1", "duration": "5.0",
            "side_data_list": [{"side_data_type": "Display Matrix", "rotation": -90}]}]})", &info));
        QCOMPARE(info.size, QSize(1080, 1920));

        QVERIFY(MediaProbe::parse(R"({"streams": [{"codec_type": "video", "width": 1920, "height": 1080,
            "avg_frame_rate": "30/1", "duration": "5.0", "tags": {"rotate": "180"}}]})", &info));
        QCOMPARE(info.size, QSize(1920, 1080));
    }

    void probeFallsBackForRateAndDuration()
    {
        // Matroska: no stream duration, and the average rate may be unknown
        MediaInfo info;
        QVERIFY(MediaProbe::parse(R"({"streams": [{"codec_type": "video", "width": 1280, "height": 720,
            "avg_frame_rate": "0/0", "r_frame_rate": "30000/1001"}], "format": {"duration": "12.5", "bit_rate": "8000000"}})", &info));
        QCOMPARE(info.fpsNum, 30000);
        QCOMPARE(info.fpsDen, 1001);
        QCOMPARE(info.duration, 12.5);
        QCOMPARE(info.bitrateKbps, 8000); // of the whole file, for want of better
        QVERIFY(!info.hasAudio);
    }

    void probeSkipsCoverArt()
    {
        MediaInfo info;
        QVERIFY(MediaProbe::parse(R"({"streams": [
            {"codec_type": "video", "codec_name": "mjpeg", "width": 600, "height": 600, "avg_frame_rate": "0/0",
             "r_frame_rate": "90000/1", "disposition": {"attached_pic": 1}},
            {"codec_type": "video", "codec_name": "hevc", "width": 3840, "height": 2160, "avg_frame_rate": "25/1",
             "duration": "3.0"}]})", &info));
        QCOMPARE(info.size, QSize(3840, 2160));
        QCOMPARE(info.videoCodec, QString("hevc"));
    }

    void probeRejectsFilesWithoutVideo()
    {
        MediaInfo info;
        QString error;
        QVERIFY(!MediaProbe::parse(R"({"streams": [{"codec_type": "audio", "codec_name": "mp3"}]})", &info, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!MediaProbe::parse("", &info, &error));
        QVERIFY(!MediaProbe::parse(R"({"streams": [{"codec_type": "video", "width": 0, "height": 0}]})", &info, &error));
    }

    // --- the real thing: needs ffmpeg ---------------------------------------

    void probeRealFile()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QString error;
        QVERIFY2(MediaProbe::probe("ffprobe", m_sourceVideo, &info, &error), qPrintable(error));
        QCOMPARE(info.size, QSize(320, 240));
        QCOMPARE(info.fps(), 30.0);
        QVERIFY(qAbs(info.duration - 2.0) < 0.1);
        QVERIFY(info.hasAudio);

        QVERIFY(!MediaProbe::probe("ffprobe", m_dir.filePath("missing.mp4"), &info, &error));
        QVERIFY(!MediaProbe::probe("/no/such/ffprobe", m_sourceVideo, &info, &error));
    }

    void detectFindsSoftwareEncoder()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QVector<VideoEncoder> found = VideoEncoders::detect("ffmpeg");
        QStringList ids;
        for (const VideoEncoder &encoder : found)
            {
                ids << encoder.id;
            }
        qInfo() << "usable encoders here:" << ids;
        // the source video above was made with it
        QVERIFY(ids.contains("libx264"));
        QVERIFY(VideoEncoders::detect("/no/such/ffmpeg").isEmpty());
    }

    void exportDrawsTheGaugeAndKeepsTheSound()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const QString output = m_dir.filePath("with-gauge.mp4");
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settingsFor(output), overlay);
        QCOMPARE(plan.overlayRect, QRect(160, 120, 60, 60));

        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        QSignalSpy progress(&job, &ExportJob::progress);
        job.start();
        QVERIFY(finished.wait(60000));
        QCOMPARE(finished.first().at(0).value<ExportJob::Outcome>(), ExportJob::Succeeded);
        QVERIFY2(finished.first().at(1).toString().isEmpty(), qPrintable(finished.first().at(1).toString()));
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(0).toLongLong(), plan.frameCount);

        MediaInfo result;
        QString error;
        QVERIFY2(MediaProbe::probe("ffprobe", output, &result, &error), qPrintable(error));
        QCOMPARE(result.size, QSize(320, 240));
        QCOMPARE(result.fps(), 30.0);
        QVERIFY2(qAbs(result.duration - info.duration) < 0.1, qPrintable(QString::number(result.duration)));
        QVERIFY(result.hasAudio);
        // not one frame lost or added
        QCOMPARE(countFrames(m_sourceVideo), 60);
        QCOMPARE(countFrames(output), 60);

        const QImage before = frameAt(m_sourceVideo, 1.0);
        const QImage after = frameAt(output, 1.0);
        QVERIFY(!before.isNull() && !after.isNull());
        QVERIFY(difference(before, after, 190, 150) > 60);   // on the dial
        QVERIFY(difference(before, after, 40, 40) < 20);     // far from it: untouched
        QVERIFY(difference(before, after, 280, 200) < 20);
    }

    void exportWithGaugesInTwoCorners()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        OverlayItem topLeft;
        topLeft.gaugeId = BuiltinGauges::DialId;
        topLeft.height = 0.25;
        OverlayItem bottomRight = topLeft;
        bottomRight.x = 0.75;
        bottomRight.y = 0.75;
        const Overlay overlay({topLeft, bottomRight}, steadySession(), Units(), GaugeTheme());
        const QString output = m_dir.filePath("two-corners.mp4");
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settingsFor(output), overlay);
        QCOMPARE(plan.regions.size(), 2);
        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        const QImage before = frameAt(m_sourceVideo, 1.0);
        const QImage after = frameAt(output, 1.0);
        QVERIFY(difference(before, after, 30, 30) > 60);      // centre of the top left dial
        QVERIFY(difference(before, after, 270, 210) > 60);    // centre of the bottom right one
        QVERIFY(difference(before, after, 160, 120) < 20);    // between them: untouched
        QVERIFY(difference(before, after, 280, 30) < 20);
    }

    void exportRangeAndSmallerSize()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        ExportSettings settings = settingsFor(m_dir.filePath("part.mkv"));
        settings.start = 0.5;
        settings.duration = 1.0;
        settings.height = 120;
        settings.audio = ExportSettings::NoAudio;
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settings, overlay);

        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));

        MediaInfo result;
        QVERIFY(MediaProbe::probe("ffprobe", settings.outputPath, &result));
        QCOMPARE(result.size, QSize(160, 120));
        QVERIFY2(qAbs(result.duration - 1.0) < 0.1, qPrintable(QString::number(result.duration)));
        QVERIFY(!result.hasAudio);
    }

    void failedExportExplainsAndLeavesNoFile()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const QString output = m_dir.filePath("broken.mp4");
        const Overlay overlay = oneGauge();
        // a source that is not there: ffmpeg starts and gives up
        const ExportPlan plan = ExportPlan::make(m_dir.filePath("missing.mp4"), info, settingsFor(output), overlay);
        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<ExportJob::Outcome>(), ExportJob::Failed);
        QVERIFY(finished.first().at(1).toString().contains("missing.mp4"));
        QVERIFY(!QFile::exists(output));
    }

    void missingFfmpegIsAFailureNotAHang()
    {
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor(m_dir.filePath("x.mp4")), overlay);
        ExportJob job("/no/such/dir/ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.count() == 1 || finished.wait(10000));
        QCOMPARE(finished.first().at(0).value<ExportJob::Outcome>(), ExportJob::Failed);
        QVERIFY(finished.first().at(1).toString().contains("/no/such/dir/ffmpeg"));
    }

    // Runs an export of a long video, stops it after the first progress report
    // and returns how it ended.
    ExportJob::Outcome exportAndStop(const QString &source, const ExportSettings &settings, qint64 *framesSent)
    {
        MediaInfo info;
        if (!MediaProbe::probe("ffprobe", source, &info))
            {
                return ExportJob::Failed;
            }
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(source, info, settings, overlay);
        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        QSignalSpy progress(&job, &ExportJob::progress);
        job.start();
        if (!progress.wait(30000) || !finished.isEmpty())
            {
                return ExportJob::Failed;
            }
        job.stop();
        job.stop(); // a second request is harmless
        if (finished.isEmpty() && !finished.wait(60000))
            {
                return ExportJob::Failed;
            }
        *framesSent = progress.last().at(0).toLongLong();
        return finished.count() == 1 ? finished.first().at(0).value<ExportJob::Outcome>() : ExportJob::Failed;
    }

    void stoppedExportIsAPlayableVideo()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString source = longSource(false);
        QVERIFY(!source.isEmpty());
        const QString output = m_dir.filePath("stopped.mp4");
        qint64 sent = 0;
        QCOMPARE(exportAndStop(source, settingsFor(output), &sent), ExportJob::Stopped);

        MediaInfo result;
        QString error;
        QVERIFY2(MediaProbe::probe("ffprobe", output, &result, &error), qPrintable(error));
        QCOMPARE(result.size, QSize(1280, 720));
        const int frames = countFrames(output);
        QVERIFY2(frames > 0 && frames < 18000, qPrintable(QString::number(frames)));
        // a real, decodable picture, with the gauge on it
        const QImage frame = frameAt(output, 0.0);
        QVERIFY(!frame.isNull());
        QVERIFY(difference(frame, frameAt(source, 0.0), 760, 480) > 60);
    }

    // Copied sound would otherwise run on to the end of the source.
    void stoppedExportHasSoundOfTheSameLength()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString source = longSource(true);
        QVERIFY(!source.isEmpty());
        for (const char *name : {"stopped-sound.mp4", "stopped-sound.mkv"})
            {
                const QString output = m_dir.filePath(name);
                qint64 sent = 0;
                QCOMPARE(exportAndStop(source, settingsFor(output), &sent), ExportJob::Stopped);

                MediaInfo result;
                QVERIFY(MediaProbe::probe("ffprobe", output, &result));
                QVERIFY(result.hasAudio);
                QVERIFY2(result.duration > 0.0 && result.duration < 300.0, qPrintable(QString::number(result.duration)));
                const int frames = countFrames(output);
                QVERIFY(frames > 0);
                if (QByteArray(name).endsWith(".mp4"))
                    {
                        const QHash<QString, double> durations = streamDurations(output);
                        QVERIFY2(qAbs(durations.value("audio") - durations.value("video")) < 0.5,
                                 qPrintable(QString("video %1 s, audio %2 s").arg(durations.value("video")).arg(durations.value("audio"))));
                    }
                // nothing left over from the rewrite
                const QStringList leftovers = QDir(m_dir.path()).entryList({"*.part.*"});
                QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(' ')));
            }
    }

    void stopBeforeStartingKeepsNothing()
    {
        const Overlay overlay = oneGauge();
        const QString output = m_dir.filePath("never.mp4");
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor(output), overlay);
        ExportJob job("ffmpeg", plan, overlay, 0.0);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.stop();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<ExportJob::Outcome>(), ExportJob::Cancelled);
        QVERIFY(!QFile::exists(output));
    }

    void destroyingARunningJobCleansUp()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString source = longSource(false);
        QVERIFY(!source.isEmpty());
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", source, &info));
        const QString output = m_dir.filePath("destroyed.mp4");
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(source, info, settingsFor(output), overlay);
        {
            ExportJob job("ffmpeg", plan, overlay, 0.0);
            QSignalSpy progress(&job, &ExportJob::progress);
            job.start();
            QVERIFY(progress.wait(30000));
        }
        QVERIFY(!QFile::exists(output));
    }
};

QTEST_MAIN(TestExport)
#include "test_export.moc"
