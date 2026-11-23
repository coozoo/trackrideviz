#include "gauge/builtingauges.h"
#include "overlay/overlay.h"
#include "telemetry/channelids.h"
#include "telemetry/gpmfimporter.h"
#include "video/cutlist.h"
#include "video/exportjob.h"
#include "video/exportplan.h"
#include "video/exportprofile.h"
#include "video/ffmpegtools.h"
#include "video/libav.h"
#include "video/mediainfo.h"
#include "video/soundtrack.h"
#include "video/videoencoders.h"
#include "video/videobackground.h"
#include "video/videojoin.h"
#include "video/videojoinjob.h"

#include <QDir>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestExport : public QObject
{
    Q_OBJECT

public:
    // The whole suite runs once for each engine (see main): what one export makes, the other must
    explicit TestExport(ExportJob::Engine engine)
        : m_engine(engine)
    {
    }

private:
    const ExportJob::Engine m_engine;

    // Tests that let an export run for a while before stopping it are left
    // out unless asked for: together they take a minute.
    static bool slowTests() { return qEnvironmentVariableIntValue("RTV_SLOW_TESTS") != 0; }

    QTemporaryDir m_dir;
    QString m_sourceVideo;   // made by ffmpeg in initTestCase; empty when ffmpeg is missing

    // A join on this run's engine, as the Join dialog runs it: why it failed, empty when it
    // worked; `progressed`: the last progress told.
    QString join(const std::function<void(VideoJoinJob &)> &start, double *progressed = nullptr)
    {
        VideoJoinJob job(m_engine);
        QSignalSpy finished(&job, &VideoJoinJob::finished);
        QSignalSpy progress(&job, &VideoJoinJob::progress);
        start(job);
        if (!finished.wait(120000))
            {
                return "the join did not end";
            }
        if (progressed && !progress.isEmpty())
            {
                *progressed = progress.last().at(0).toDouble();
            }
        const QString error = finished.first().at(1).toString();
        return finished.first().at(0).toBool() ? QString() : error.isEmpty() ? QString("failed, saying nothing") : error;
    }

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
        // 2 s, 320x240, 30 fps, flat gray, with a tone
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

    void planGaugesOnly()
    {
        OverlayItem topLeft;
        topLeft.gaugeId = BuiltinGauges::DialId;
        topLeft.height = 0.25;
        OverlayItem bottomRight = topLeft;
        bottomRight.x = 0.75;
        bottomRight.y = 0.75;
        const Overlay overlay({topLeft, bottomRight}, steadySession(), Units(), GaugeTheme());
        ExportSettings settings = settingsFor("gauges.mov");
        settings.picture = ExportSettings::GaugesOnly;
        settings.height = 720;
        Soundtrack music;
        music.tracks = {{"song.mp3", 60.0, 0.0, 0.0}};
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, overlay, {}, music);
        QVERIFY(plan.transparent);
        QVERIFY(!plan.hasAudio);
        // nothing of the video is read: the gauges are the only input
        QVERIFY(!plan.arguments.contains("in.mp4"));
        QVERIFY(!plan.arguments.contains("song.mp3"));
        QCOMPARE(valueOf(plan.arguments, "-i"), QString("pipe:0"));
        QCOMPARE(valueOf(plan.arguments, "-filter_complex"),
                 QString("color=c=black@0:s=1280x720:r=60/1,format=rgba[base];"
                         "[0:v]split=2[s0][s1];[s0]crop=180:180:0:0[g0];[s1]crop=180:180:0:180[g1];"
                         "[base][g0]overlay=0:0:alpha=straight:format=auto:eof_action=endall[o0];"
                         "[o0][g1]overlay=960:540:alpha=straight:format=auto:eof_action=endall[v]"));
        QVERIFY(hasPair(plan.arguments, "-c:v", "prores_ks"));
        QVERIFY(hasPair(plan.arguments, "-pix_fmt", "yuva444p10le"));
        QVERIFY(plan.arguments.contains("-an"));
        QCOMPARE(plan.frameCount, qint64(6000));

        settings.outputPath = "gauges.webm";
        const ExportPlan webm = ExportPlan::make("in.mp4", fullHd60(), settings, overlay);
        QVERIFY(hasPair(webm.arguments, "-c:v", "libvpx-vp9"));
        QVERIFY(hasPair(webm.arguments, "-pix_fmt", "yuva420p"));
        QVERIFY(!webm.arguments.contains("-movflags"));
    }

    void planWithoutAFrameRateCountsNoTime()
    {
        ExportPlan plan;
        plan.startTime = 3.0;
        QCOMPARE(plan.frameTime(10), 3.0);
        QCOMPARE(plan.lengthOf(10), 0.0);
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
        QVERIFY(hasPair(plan.arguments, "-b:a", "192k"));

        settings.outputPath = "out.mkv";
        settings.audio = ExportSettings::OpusAudio;
        settings.audioKbps = 128;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-c:a", "libopus"));
        QVERIFY(hasPair(plan.arguments, "-b:a", "128k"));
        // no more than Opus takes for a channel: the 384 of a YouTube upload would stop it on mono sound
        settings.audioKbps = 384;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-b:a", "256k"));

        settings.audio = ExportSettings::VorbisAudio;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(hasPair(plan.arguments, "-c:a", "libvorbis"));
        QVERIFY(ExportSettings::needsMatroska(settings.audio));
        QVERIFY(!ExportSettings::needsMatroska(ExportSettings::AacAudio));

        settings.audio = ExportSettings::NoAudio;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QVERIFY(plan.arguments.contains("-an"));
        QVERIFY(!plan.arguments.contains("0:a?"));
    }

    void planKeepsItsParts()
    {
        // what an engine of the program's own reads: the inputs in their order, the graph,
        // where the sound comes from and the options of each part; ffmpeg's command is made of them
        ExportSettings settings = settingsFor("out.mp4");
        settings.start = 10.0;
        settings.audio = ExportSettings::AacAudio;
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        QCOMPARE(plan.inputs.size(), 2);
        QCOMPARE(plan.inputs.at(0).source, QString("in.mp4"));
        QVERIFY(hasPair(plan.inputs.at(0).options, "-ss", "10.000"));
        QCOMPARE(plan.inputs.at(1).source, QString(ExportPlan::Pipe));
        QVERIFY(hasPair(plan.inputs.at(1).options, "-f", "rawvideo"));
        QVERIFY(plan.filterGraph.startsWith("[0:v][1:v]overlay=") && plan.filterGraph.contains("[v]"));
        QCOMPARE(plan.audioMap, QString("0:a?"));
        QVERIFY(hasPair(plan.audioOptions, "-c:a", "aac"));
        QVERIFY(hasPair(plan.videoOptions, "-c:v", "libx264"));
        QCOMPARE(plan.arguments, plan.ffmpegArguments());
        QCOMPARE(plan.arguments.last(), QString("out.mp4"));

        settings.audio = ExportSettings::NoAudio;
        QVERIFY(ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge()).audioMap.isEmpty());
    }

    // --- music -------------------------------------------------------------

    void soundtrackTimes()
    {
        Soundtrack soundtrack;
        soundtrack.tracks = {{"a.mp3", 10.0, 0.0, 0.0}, {"b.ogg", 20.0, 2.0, 7.0}, {"c.wav", 0.0, 0.0, 0.0}};
        soundtrack.start = 4.0;
        soundtrack.fade = 1.0;
        QCOMPARE(soundtrack.tracks.at(1).length(), 5.0);
        QCOMPARE(soundtrack.length(), 15.0);

        QCOMPARE(soundtrack.musicTime(3.0, {}), -1.0);
        QCOMPARE(soundtrack.musicTime(9.0, {}), 5.0);
        // the music runs on through what is cut: those seconds do not count
        QCOMPARE(soundtrack.musicTime(9.0, {{5.0, 7.0}}), 3.0);
        QCOMPARE(soundtrack.musicTime(9.0, {{1.0, 2.0}}), 5.0);

        QCOMPARE(soundtrack.at(-0.5).track, -1);
        Soundtrack::Moment moment = soundtrack.at(5.0);
        QCOMPARE(moment.track, 0);
        QCOMPARE(moment.position, 5.0);
        QCOMPARE(moment.gain, 1.0);
        QCOMPARE(soundtrack.at(0.25).gain, 0.25);
        QCOMPARE(soundtrack.at(9.5).gain, 0.5);
        moment = soundtrack.at(11.0);
        QCOMPARE(moment.track, 1);
        QCOMPARE(moment.position, 3.0); // one second into the part that plays, from 2 s
        QCOMPARE(moment.gain, 1.0);
        // a file of unknown length plays to its own end
        moment = soundtrack.at(100.0);
        QCOMPARE(moment.track, 2);
        QCOMPARE(moment.position, 85.0);

        soundtrack.tracks.removeLast();
        QCOMPARE(soundtrack.at(15.5).track, -1);
        QVERIFY(soundtrack.changesSound());
        soundtrack.musicMuted = true;
        QVERIFY(!soundtrack.changesSound());
        soundtrack.videoVolume = 50;
        QVERIFY(soundtrack.changesSound());
    }

    void planMixesMusic()
    {
        Soundtrack soundtrack;
        soundtrack.tracks = {{"/m/a.mp3", 10.0, 0.0, 0.0}, {"/m/b.ogg", 20.0, 2.0, 7.0}};
        soundtrack.start = 5.0;
        soundtrack.fade = 1.0;
        soundtrack.videoVolume = 50;
        soundtrack.musicVolume = 80;
        // the sound would be copied: with music it is made anew
        ExportSettings settings = settingsFor("out.mp4");
        ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack);
        QVERIFY(plan.hasAudio);
        // after the video and the gauges
        QCOMPARE(plan.arguments.mid(plan.arguments.indexOf("/m/a.mp3") - 3, 4),
                 QStringList({"-i", "pipe:0", "-i", "/m/a.mp3"}));
        QVERIFY(hasPair(plan.arguments, "-i", "/m/b.ogg"));
        QVERIFY(hasPair(plan.arguments, "-c:a", "aac"));
        QVERIFY(hasPair(plan.arguments, "-map", "[a]"));
        QString graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY2(graph.contains("[2:a]atrim=start=0.000:end=10.000,asetpts=PTS-STARTPTS,"
                                "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo,"
                                "afade=t=in:d=1.000,afade=t=out:st=9.000:d=1.000[m0]"), qPrintable(graph));
        QVERIFY2(graph.contains("[3:a]atrim=start=2.000:end=7.000"), qPrintable(graph));
        QVERIFY2(graph.contains("[m0][m1]concat=n=2:v=0:a=1,adelay=delays=5000:all=1,volume=0.800,atrim=end=100.000[mus]"),
                 qPrintable(graph));
        QVERIFY2(graph.contains("[0:a]volume=0.500,"), qPrintable(graph));
        QVERIFY2(graph.contains("[vs][mus]amix=inputs=2:duration=first:dropout_transition=0:normalize=0[a]"), qPrintable(graph));

        // louder than recorded: held under full scale by a limiter at the end
        soundtrack.videoVolume = 150;
        graph = valueOf(ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack).arguments,
                        "-filter_complex");
        QVERIFY2(graph.contains("[0:a]volume=1.500,"), qPrintable(graph));
        QVERIFY2(graph.endsWith("normalize=0[am];[am]alimiter=limit=0.97:level=0[a]"), qPrintable(graph));
        soundtrack.videoVolume = 50;

        // the music giving way to the camera's sound, which is heard as it is
        soundtrack.ducking = true;
        graph = valueOf(ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack).arguments,
                        "-filter_complex");
        QVERIFY2(graph.contains(";[vs]asplit=2[vsm][vsk];"), qPrintable(graph));
        QVERIFY2(graph.contains("[mus][vsk]sidechaincompress="), qPrintable(graph));
        QVERIFY2(graph.endsWith("[vsm][mud]amix=inputs=2:duration=first:dropout_transition=0:normalize=0[a]"), qPrintable(graph));
        soundtrack.ducking = false;

        // an export from 8 s: three seconds of the music are gone already
        settings.start = 8.0;
        settings.duration = 10.0;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack);
        graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY2(graph.contains("atrim=start=3.000,asetpts=PTS-STARTPTS,volume=0.800,atrim=end=10.000[mus]"), qPrintable(graph));

        // the video's own sound off: only the music
        settings = settingsFor("out.mp4");
        soundtrack.videoMuted = true;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, Overlay(), {}, soundtrack);
        graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY2(graph.contains("[1:a]atrim"), qPrintable(graph)); // no gauges: music from input 1
        QVERIFY(graph.endsWith("atrim=end=100.000[a]"));
        QVERIFY(!graph.contains("amix"));

        // no music, the video's sound quieter
        soundtrack.videoMuted = false;
        soundtrack.musicMuted = true;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {{10.0, 20.0}}, soundtrack);
        graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY(!plan.arguments.contains("/m/a.mp3"));
        QVERIFY2(graph.contains("[ac];[ac]volume=0.500,aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[a]"),
                 qPrintable(graph));

        // the video's sound off and no music: none
        soundtrack.videoMuted = true;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack);
        QVERIFY(!plan.hasAudio);
        QVERIFY(plan.arguments.contains("-an"));

        // music of which nothing is left to play (a project edited by hand) is no music
        Soundtrack nothingLeft;
        nothingLeft.tracks = {{"/m/a.mp3", 10.0, 12.0, 0.0}};
        nothingLeft.videoMuted = true;
        QVERIFY(!nothingLeft.hasMusic());
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, nothingLeft);
        QVERIFY(!plan.hasAudio);
        QVERIFY(plan.arguments.contains("-an"));
        QVERIFY(!plan.arguments.contains("/m/a.mp3"));

        // asked for no sound: no music either
        settings.audio = ExportSettings::NoAudio;
        soundtrack.videoMuted = false;
        soundtrack.musicMuted = false;
        plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {}, soundtrack);
        QVERIFY(!plan.hasAudio);
        QVERIFY(!plan.arguments.contains("/m/a.mp3"));
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

    // --- profiles ---------------------------------------------------------

    void youTubeBitrates()
    {
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(1920, 1080), 30.0), 8000);
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(1920, 1080), 59.94), 12000);
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(3840, 2160), 60.0), 68000);
        // upright video counts by its shorter side
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(1080, 1920), 30.0), 8000);
        // 2.7K is near 1440p
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(2704, 1520), 60.0), 24000);
        QCOMPARE(ExportProfiles::youTubeKbps(QSize(320, 240), 30.0), 1000);
    }

    void profilesFillTheChoices()
    {
        const QVector<ExportProfile> builtIn = ExportProfiles::builtIn();
        QCOMPARE(builtIn.size(), 3);
        const ExportProfile youTube = builtIn.at(0);
        const ExportProfile small = builtIn.at(1);
        QCOMPARE(youTube.id, QString("youtube"));
        QCOMPARE(small.id, QString("small"));

        MediaInfo info = fullHd60();
        info.bitrateKbps = 30000;
        const QVector<VideoEncoder> available{VideoEncoders::byId("libx264"), VideoEncoders::byId("libx265"),
                                              VideoEncoders::byId("h264_vaapi"), VideoEncoders::byId("hevc_vaapi")};
        ExportSettings current = settingsFor("/v/out.mp4");
        current.encoder = "h264_vaapi";

        ExportSettings settings = ExportProfiles::apply(youTube, current, info, available);
        // the graphics card, as before
        QCOMPARE(settings.encoder, QString("h264_vaapi"));
        QCOMPARE(settings.rateControl, ExportSettings::Bitrate);
        QCOMPARE(settings.bitrateKbps, 12000);
        QCOMPARE(settings.audio, ExportSettings::AacAudio);
        QCOMPARE(settings.audioKbps, 384);
        QCOMPARE(settings.outputPath, QString("/v/out.mp4"));

        settings = ExportProfiles::apply(small, current, info, available);
        QCOMPARE(settings.encoder, QString("hevc_vaapi"));
        QCOMPARE(settings.bitrateKbps, 12000); // 40 % of the video's
        QCOMPARE(settings.audio, ExportSettings::OpusAudio);
        QCOMPARE(settings.outputPath, QString("/v/out.mkv"));

        current.encoder = "libx264";
        settings = ExportProfiles::apply(small, current, info, available);
        QCOMPARE(settings.encoder, QString("libx265"));
        // a video small already: no less than that share of what YouTube takes
        info.bitrateKbps = 3000;
        QCOMPARE(ExportProfiles::apply(small, current, info, available).bitrateKbps, 4800);
        // no HEVC here: whatever there is
        settings = ExportProfiles::apply(small, current, info, {VideoEncoders::byId("libx264")});
        QCOMPARE(settings.encoder, QString("libx264"));
        // AV1 first, where there is an encoder for it
        settings = ExportProfiles::apply(small, current, info, available + QVector<VideoEncoder>{VideoEncoders::byId("libsvtav1")});
        QCOMPARE(settings.encoder, QString("libsvtav1"));
        const QStringList args = VideoEncoders::arguments(VideoEncoders::byId("libsvtav1"), settings);
        QVERIFY(hasPair(args, "-preset", "5"));
        // a card's AV1 by the bitrate only: each has a scale of its own
        QVERIFY(!VideoEncoders::byId("av1_nvenc").hasQuality);
    }

    void ownProfiles()
    {
        ExportSettings chosen = settingsFor("/v/out.mkv");
        chosen.encoder = "libx265";
        chosen.rateControl = ExportSettings::Quality;
        chosen.quality = 27;
        chosen.speed = ExportSettings::Small;
        chosen.height = 720;
        chosen.audio = ExportSettings::VorbisAudio;
        chosen.audioKbps = 160;
        const ExportProfile profile = ExportProfile::fromJson(ExportProfile::of("Mine", chosen).toJson());
        QCOMPARE(profile.name, QString("Mine"));
        QVERIFY(!profile.builtIn);
        QCOMPARE(profile.suffix, QString("mkv"));

        const ExportSettings applied = ExportProfiles::apply(profile, settingsFor("/v/other.mp4"), fullHd60(),
                                                             {VideoEncoders::byId("libx264"), VideoEncoders::byId("libx265")});
        QCOMPARE(applied.outputPath, QString("/v/other.mkv"));
        QVERIFY(ExportProfiles::sameChoices(applied, chosen));
        ExportSettings changed = applied;
        changed.quality = 28;
        QVERIFY(!ExportProfiles::sameChoices(changed, chosen));
    }

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
                QCOMPARE(encoder.codec == VideoEncoder::Hevc, encoder.id.contains("hevc") || encoder.id.contains("265"));
                QCOMPARE(encoder.codec == VideoEncoder::Av1, encoder.id.contains("av1"));
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
        // without ffprobe the program's own libraries read it, when it has them
        QCOMPARE(MediaProbe::probe("/no/such/ffprobe", m_sourceVideo, &info, &error), Libav::available());
        QVERIFY(!MediaProbe::probeWithFfprobe("/no/such/ffprobe", m_sourceVideo, &info, &error));
    }

    void libavReadsAsFfprobeDoes()
    {
        if (!Libav::available())
            {
                QSKIP("built without FFmpeg's libraries");
            }
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        QVERIFY(Libav::version().startsWith("FFmpeg "));
        // the test video, and a copy that a phone held upright would make: turned by its display matrix
        const QString rotated = m_dir.filePath("rotated.mp4");
        QVERIFY(QProcess::execute("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-display_rotation", "90",
                                             "-i", m_sourceVideo, "-c", "copy", rotated}) == 0);
        for (const QString &path : {m_sourceVideo, rotated})
            {
                MediaInfo own;
                MediaInfo theirs;
                QString error;
                QVERIFY2(Libav::probe(path, &own, &error), qPrintable(error));
                QVERIFY2(MediaProbe::probeWithFfprobe("ffprobe", path, &theirs, &error), qPrintable(error));
                QCOMPARE(own.size, theirs.size);
                QCOMPARE(own.fpsNum, theirs.fpsNum);
                QCOMPARE(own.fpsDen, theirs.fpsDen);
                QVERIFY(qAbs(own.duration - theirs.duration) < 0.001);
                QCOMPARE(own.frameCount, theirs.frameCount);
                QCOMPARE(own.bitrateKbps, theirs.bitrateKbps);
                QCOMPARE(own.videoCodec, theirs.videoCodec);
                QCOMPARE(own.hasAudio, theirs.hasAudio);
                QCOMPARE(own.audioCodec, theirs.audioCodec);
                QCOMPARE(own.videoTimeBase, theirs.videoTimeBase);
                QCOMPARE(own.videoSetup, theirs.videoSetup);
                QCOMPARE(own.audioSampleRate, theirs.audioSampleRate);
                QCOMPARE(own.audioChannels, theirs.audioChannels);
                QCOMPARE(own.audioTimeBase, theirs.audioTimeBase);
                QCOMPARE(own.created, theirs.created);
            }
        MediaInfo turned;
        QVERIFY(Libav::probe(rotated, &turned));
        QCOMPARE(turned.size, QSize(240, 320));
        QString error;
        MediaInfo none;
        QVERIFY(!Libav::probe(m_dir.filePath("missing.mp4"), &none, &error));
        QVERIFY(!error.isEmpty());
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

    void detectOwnFindsTheLibrariesEncoders()
    {
        if (m_engine != ExportJob::OwnEngine)
            {
                QSKIP("about the program's own engine");
            }
        const QVector<VideoEncoder> own = VideoEncoders::detectOwn();
        QStringList ids;
        for (const VideoEncoder &encoder : own)
            ids << encoder.id;
        // what the libraries were built with; the graphics card's only when it really encodes
        QVERIFY2(!own.isEmpty(), "no encoder at all");
        for (const VideoEncoder &encoder : VideoEncoders::known())
            {
                if (!encoder.hardware)
                    QCOMPARE(ids.contains(encoder.id), Libav::hasEncoder(encoder.id));
            }
        QVERIFY(!Libav::hasEncoder("no_such_encoder"));
        QVERIFY(!Libav::encoderWorks("no_such_encoder", {}));
        qInfo() << "encoders of the program's own engine:" << ids.join(' ');
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

        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
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
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        const QImage before = frameAt(m_sourceVideo, 1.0);
        const QImage after = frameAt(output, 1.0);
        QVERIFY(difference(before, after, 30, 30) > 60);      // center of the top left dial
        QVERIFY(difference(before, after, 270, 210) > 60);    // center of the bottom right one
        QVERIFY(difference(before, after, 160, 120) < 20);    // between them: untouched
        QVERIFY(difference(before, after, 280, 30) < 20);
    }

    // No video: the gauges over a plain color, as long as asked for, with music under them.
    void exportWithoutAVideo()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString song = m_dir.filePath("background-song.wav");
        QVERIFY(FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                            "sine=frequency=440:duration=5", song}).exitCode == 0);
        Soundtrack music;
        music.tracks = {{song, 5.0, 0.0, 0.0}};
        VideoBackground background;
        background.size = QSize(320, 240);
        background.fps = 30;
        background.color = QColor(0, 90, 40);
        const MediaInfo info = background.mediaInfo(2.0);
        const QString output = m_dir.filePath("no-video.mp4");
        ExportSettings settings = settingsFor(output);
        settings.background = background.color;
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(QString(), info, settings, overlay, {}, music);
        // the gauges are input 0, the music 1; nothing else is read
        QCOMPARE(plan.inputs.size(), 2);
        QCOMPARE(plan.inputs.first().source, QString(ExportPlan::Pipe));
        QVERIFY(plan.filterGraph.startsWith("color=c=0x005a28:s=320x240:r=30/1[base]"));
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        MediaInfo result;
        QVERIFY(MediaProbe::probe("ffprobe", output, &result));
        QCOMPARE(result.size, background.size);
        QCOMPARE(result.fps(), 30.0);
        QVERIFY(result.hasAudio);
        QCOMPARE(countFrames(output), 60);
        const QImage frame = frameAt(output, 1.0);
        QVERIFY(!frame.isNull());
        // the color away from the dial, the dial below right of the middle
        const QColor corner = frame.pixelColor(20, 20);
        QVERIFY2(std::abs(corner.green() - 90) < 12 && corner.red() < 12, qPrintable(corner.name()));
        QVERIFY(frame.pixelColor(190, 150) != corner);
        // without gauges: the color alone, still two seconds
        const QString plain = m_dir.filePath("no-video-plain.mp4");
        settings.outputPath = plain;
        const ExportPlan nothing = ExportPlan::make(QString(), info, settings, Overlay());
        QVERIFY(nothing.inputs.isEmpty());
        ExportJob plainJob("ffmpeg", nothing, Overlay(), 0.0, nullptr, m_engine);
        QSignalSpy plainFinished(&plainJob, &ExportJob::finished);
        plainJob.start();
        QVERIFY(plainFinished.wait(60000));
        QVERIFY2(plainFinished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(plainFinished.first().at(1).toString()));
        QCOMPARE(countFrames(plain), 60);
    }

    void exportGaugesOnly_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("decoder");
        QTest::newRow("prores") << "mov" << QString();
        // ffmpeg's own VP9 decoder drops the transparency; libvpx keeps it
        QTest::newRow("vp9") << "webm" << "libvpx-vp9";
    }

    void exportGaugesOnly()
    {
        QFETCH(QString, suffix);
        QFETCH(QString, decoder);
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const Overlay overlay = oneGauge();
        const QString output = m_dir.filePath("gauges." + suffix);
        ExportSettings settings = settingsFor(output);
        settings.picture = ExportSettings::GaugesOnly;
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settings, overlay);
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        QVERIFY(!streamDurations(output).contains("audio"));
        QCOMPARE(countFrames(output), countFrames(m_sourceVideo));

        const QString png = m_dir.filePath("gauges-frame.png");
        QFile::remove(png);
        QStringList args{"-hide_banner", "-loglevel", "error", "-y"};
        if (!decoder.isEmpty())
            {
                args << "-c:v" << decoder;
            }
        args << "-ss" << "1" << "-i" << output << "-frames:v" << "1" << "-pix_fmt" << "rgba" << png;
        QCOMPARE(FfmpegTools::run("ffmpeg", args).exitCode, 0);
        const QImage frame = QImage(png).convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(frame.size(), info.size);
        // nothing where there is no gauge; the dial itself solid, in its own colors
        QCOMPARE(qAlpha(frame.pixel(10, 10)), 0);
        QCOMPARE(qAlpha(frame.pixel(frame.width() / 4, frame.height() * 3 / 4)), 0);
        const QRect dial = plan.regions.first();
        const QRgb center = frame.pixel(dial.center());
        QVERIFY2(qAlpha(center) > 240, qPrintable(QString::number(qAlpha(center))));
        // as Qt draws it: see-through parts keep their colors, not darkened towards black
        QImage drawn(info.size, QImage::Format_ARGB32_Premultiplied);
        drawn.fill(Qt::transparent);
        {
            QPainter painter(&drawn);
            overlay.render(painter, QRectF(QPointF(0, 0), QSizeF(info.size)), 1.0, dial);
        }
        drawn = drawn.convertToFormat(QImage::Format_ARGB32);
        double colorError = 0.0;
        double alphaError = 0.0;
        int compared = 0;
        for (int x = dial.left(); x <= dial.right(); ++x)
            {
                for (int y = dial.top(); y <= dial.bottom(); ++y)
                    {
                        const QRgb want = drawn.pixel(x, y);
                        const QRgb got = frame.pixel(x, y);
                        alphaError += qAbs(qAlpha(want) - qAlpha(got));
                        // the see-through parts, where multiplied colors would show
                        if (qAlpha(want) > 60 && qAlpha(want) < 200)
                            {
                                colorError += qAbs(qRed(want) - qRed(got)) + qAbs(qGreen(want) - qGreen(got))
                                              + qAbs(qBlue(want) - qBlue(got));
                                ++compared;
                            }
                    }
            }
        const int pixels = dial.width() * dial.height();
        QVERIFY2(compared > 100, qPrintable(QString::number(compared)));
        QVERIFY2(alphaError / pixels < 4.0, qPrintable(QString::number(alphaError / pixels)));
        // multiplied colors are off by about 9 here
        QVERIFY2(colorError / compared < 5.0, qPrintable(QString::number(colorError / compared)));
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

        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
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

    void cutList()
    {
        const QVector<TimeRange> cuts = CutList::normalized({{5.0, 8.0}, {1.0, 2.0}, {7.0, 9.0}, {-3.0, -1.0}}, 20.0);
        QCOMPARE(cuts.size(), 2);
        QVERIFY(cuts.at(0) == (TimeRange{1.0, 2.0}));
        QVERIFY(cuts.at(1) == (TimeRange{5.0, 9.0}));
        const QVector<TimeRange> kept = CutList::kept(cuts, 0.0, 10.0);
        QCOMPARE(kept.size(), 3);
        QCOMPARE(CutList::keptLength(kept), 5.0);
        QCOMPARE(CutList::sourceTime(kept, 0.5), 0.5);
        QCOMPARE(CutList::sourceTime(kept, 1.5), 2.5);   // past the first cut
        QCOMPARE(CutList::sourceTime(kept, 4.5), 9.5);
        // taking a piece back out of a cut splits it
        const QVector<TimeRange> split = CutList::removed(cuts, {6.0, 7.0});
        QCOMPARE(split.size(), 3);
        QVERIFY(split.at(1) == (TimeRange{5.0, 6.0}));
        QVERIFY(split.at(2) == (TimeRange{7.0, 9.0}));
        QCOMPARE(CutList::added(split, {5.5, 7.5}).size(), 2);
        QVERIFY(CutList::cutAt(cuts, 6.0));
        QVERIFY(!CutList::cutAt(cuts, 3.0));
    }

    void stopsFromTheData()
    {
        // moving, standing 20 s from 30 s on, a short stop of 5 s at 80 s, then moving to 120 s
        auto session = std::make_shared<TelemetrySession>();
        Channel &speed = session->channelForWriting(ChannelId::Speed, "m/s");
        for (double t = 0.0; t <= 120.0; t += 1.0)
            {
                const bool standing = (t >= 30.0 && t < 50.0) || (t >= 80.0 && t < 85.0);
                speed.append(t, standing ? 0.1 : 8.0);
            }
        // data 10 s ahead of the video
        const QVector<TimeRange> stops = CutList::stops(*session, 10.0, 200.0);
        QCOMPARE(stops.size(), 1);
        QVERIFY(std::fabs(stops.first().start - 22.0) < 1e-9);   // 30 + 2 s margin - 10 s offset
        QVERIFY(std::fabs(stops.first().end - 38.0) < 1e-9);
    }

    void planLeavesOutCuts()
    {
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settingsFor("out.mp4"), oneGauge(),
                                                 {{10.0, 20.0}});
        QCOMPARE(plan.kept.size(), 2);
        QCOMPARE(plan.frameTime(0), 0.0);
        QVERIFY(std::fabs(plan.frameTime(600) - 20.0) < 1e-9);    // the 10 s mark of the output
        const QString graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY2(graph.contains("select=gte(t\\,0.000)*lt(t\\,10.000)+gte(t\\,20.000)"), qPrintable(graph));
        QVERIFY(graph.contains("setpts=N/FRAME_RATE/TB"));
        // a cut outside the exported range changes nothing
        ExportSettings range = settingsFor("out.mp4");
        range.start = 30.0;
        range.duration = 5.0;
        QVERIFY(ExportPlan::make("in.mp4", fullHd60(), range, oneGauge(), {{10.0, 20.0}}).kept.isEmpty());
    }

    void planSpedUp()
    {
        ExportSettings settings = settingsFor("out.mp4");
        settings.speedUp = 4.0;
        const ExportPlan plan = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge());
        // a quarter of the frames, each four times as far into the video
        QVERIFY(std::fabs(plan.frameTime(60) - 4.0) < 1e-9);
        QVERIFY(std::fabs(plan.lengthOf(60) - 1.0) < 1e-9);
        QCOMPARE(plan.frameCount, ExportPlan::make("in.mp4", fullHd60(), settingsFor("out.mp4"), oneGauge()).frameCount / 4);
        const QString graph = valueOf(plan.arguments, "-filter_complex");
        QVERIFY2(graph.contains("setpts=(PTS-STARTPTS)/4,fps=60/1"), qPrintable(graph));
        // the camera's sound, sped up, is left out
        QVERIFY(plan.audioMap.isEmpty());
        // with cuts: what is kept, then sped up
        const ExportPlan cut = ExportPlan::make("in.mp4", fullHd60(), settings, oneGauge(), {{10.0, 20.0}});
        QVERIFY(std::fabs(cut.frameTime(150) - 20.0) < 1e-9);    // 2.5 s of the output: 10 s kept
        const QString cutGraph = valueOf(cut.arguments, "-filter_complex");
        QVERIFY2(cutGraph.contains("setpts=N/FRAME_RATE/TB,setpts=PTS/4,fps=60/1"), qPrintable(cutGraph));
    }

    void exportSpedUp()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const QString output = m_dir.filePath("faster.mp4");
        const Overlay overlay = oneGauge();
        ExportSettings settings = settingsFor(output);
        settings.speedUp = 2.0;
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settings, overlay);
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        // 2 s at 30 fps played twice as fast: 1 s, every other frame, still 30 fps, no sound
        QVERIFY2(std::abs(countFrames(output) - 30) <= 1, qPrintable(QString::number(countFrames(output))));
        MediaInfo result;
        QVERIFY(MediaProbe::probe("ffprobe", output, &result));
        QCOMPARE(result.fps(), 30.0);
        QVERIFY2(std::fabs(result.duration - 1.0) < 0.1, qPrintable(QString::number(result.duration)));
        QVERIFY(!result.hasAudio);
        // the picture half a second in is the video's at one second
        QVERIFY(difference(frameAt(output, 0.5), frameAt(m_sourceVideo, 1.0), 40, 40) < 20);
    }

    void exportLeavesOutCuts()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const QString output = m_dir.filePath("cut.mp4");
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settingsFor(output), overlay, {{0.5, 1.2}});
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.start();
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                 qPrintable(finished.first().at(1).toString()));
        // 2 s less 0.7 s at 30 fps, and the sound as long
        QVERIFY2(std::abs(countFrames(output) - 39) <= 1, qPrintable(QString::number(countFrames(output))));
        const QHash<QString, double> durations = streamDurations(output);
        QVERIFY2(std::fabs(durations.value("video") - 1.3) < 0.1, qPrintable(QString::number(durations.value("video"))));
        QVERIFY2(std::fabs(durations.value("audio") - 1.3) < 0.1, qPrintable(QString::number(durations.value("audio"))));
    }

    void exportMixesMusic()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        // 1.5 s of a higher tone, and a second file of 3 s
        const QString first = m_dir.filePath("first.wav");
        const QString second = m_dir.filePath("second.flac");
        for (const auto &[path, length] : {std::pair{first, "1.5"}, std::pair{second, "3"}})
            {
                const FfmpegTools::RunResult made = FfmpegTools::run("ffmpeg",
                {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", QString("sine=frequency=880:duration=%1").arg(length), path});
                QVERIFY(made.started && made.exitCode == 0);
            }
        const bool own = m_engine == ExportJob::OwnEngine;
        QCOMPARE(std::round(SoundtrackMix::probeDuration("ffprobe", second, own)), 3.0);
        QCOMPARE(SoundtrackMix::probeDuration("ffprobe", m_dir.filePath("missing.wav"), own), 0.0);
        // either engine stands in for the other
        QCOMPARE(std::round(SoundtrackMix::probeDuration("/no/such/ffprobe", second, own)), Libav::available() ? 3.0 : 0.0);

        Soundtrack soundtrack;
        soundtrack.tracks = {{first, 1.5, 0.0, 0.0}, {second, 3.0, 0.5, 0.0}};
        soundtrack.start = 0.3;
        soundtrack.fade = 0.2;
        soundtrack.videoVolume = 60;
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", m_sourceVideo, &info));
        const Overlay overlay = oneGauge();
        for (const QString &name : {QString("music.mp4"), QString("music.mkv")})
            {
                const QString output = m_dir.filePath(name);
                ExportSettings settings = settingsFor(output);
                if (name.endsWith("mkv"))
                    {
                        settings.audio = ExportSettings::OpusAudio;
                    }
                const ExportPlan plan = ExportPlan::make(m_sourceVideo, info, settings, overlay, {{0.5, 0.8}}, soundtrack);
                ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
                QSignalSpy finished(&job, &ExportJob::finished);
                job.start();
                QVERIFY(finished.wait(60000));
                QVERIFY2(finished.first().at(0).value<ExportJob::Outcome>() == ExportJob::Succeeded,
                         qPrintable(finished.first().at(1).toString()));
                const FfmpegTools::RunResult probe = FfmpegTools::run("ffprobe",
                {"-v", "error", "-select_streams", "a", "-show_entries", "stream=codec_name", "-of", "csv=p=0", output});
                QCOMPARE(QString::fromLatin1(probe.output).trimmed(), name.endsWith("mkv") ? QString("opus") : QString("aac"));
                if (name.endsWith("mp4"))
                    {
                        // as long as the picture: 2 s less the cut
                        const QHash<QString, double> durations = streamDurations(output);
                        QVERIFY2(std::fabs(durations.value("audio") - 1.7) < 0.1, qPrintable(QString::number(durations.value("audio"))));
                    }
            }
    }

    void joinOrderAndShifts()
    {
        const QStringList ordered = VideoJoin::ordered({"/v/GX020124.MP4", "/v/GX010125.MP4", "/v/GX010124.MP4",
                                                        "/v/GOPR0100.MP4", "/v/GP010100.MP4"});
        QCOMPARE(ordered, QStringList({"/v/GOPR0100.MP4", "/v/GP010100.MP4", "/v/GX010124.MP4", "/v/GX020124.MP4",
                                       "/v/GX010125.MP4"}));
        QCOMPARE(VideoJoin::ordered({"/v/clip10.mp4", "/v/clip9.mp4"}), QStringList({"/v/clip9.mp4", "/v/clip10.mp4"}));

        MediaInfo minute = fullHd60();
        minute.duration = 60.0;
        MediaInfo half = minute;
        half.duration = 30.0;
        // recorded at 07:49, 07:51 (a minute's break after the first) and straight on at 07:51:30
        const QVector<VideoJoin::Part> recordings{{"/v/20250715_074900.mp4", minute}, {"/v/20250715_075100.mp4", half},
            {"/v/20250715_075130.mp4", minute}};
        QVERIFY(VideoJoin::whyNot(recordings).isEmpty());
        const QVector<TimeShift> shifts = VideoJoin::shifts(recordings);
        QCOMPARE(shifts.size(), 1);
        QVERIFY(shifts.first() == (TimeShift{60.0, 60.0}));
        // chapters without clocks follow on
        QVERIFY(VideoJoin::shifts({{"/v/GX010001.MP4", minute}, {"/v/GX020001.MP4", half}}).isEmpty());
        MediaInfo other = minute;
        other.size = QSize(1280, 720);
        QVERIFY(!VideoJoin::whyNot({{"/v/a.mp4", minute}, {"/v/b.mp4", other}}).isEmpty());
        // the camera's file and one of ours: same codec and size, another encoder's setup
        MediaInfo camera = minute;
        camera.videoSetup = "3745b6";
        MediaInfo exported = minute;
        exported.videoSetup = "9c01aa";
        QVERIFY(!VideoJoin::whyNot({{"/v/a.mp4", camera}, {"/v/b.mkv", exported}}).isEmpty());
        QVERIFY(VideoJoin::whyNot({{"/v/a.mp4", camera}, {"/v/b.mp4", camera}}).isEmpty());
        MediaInfo mono = minute;
        mono.audioChannels = 1;
        QVERIFY(!VideoJoin::whyNot({{"/v/a.mp4", minute}, {"/v/b.mp4", mono}}).isEmpty());
        QVERIFY(VideoJoin::listFile({"/v/it's.mp4"}).contains("file '/v/it'\\''s.mp4'"));
    }

    void joinTwoVideos()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString output = m_dir.filePath("joined.mp4");
        QString error = join([&](VideoJoinJob &job) { job.startCopy({m_sourceVideo, m_sourceVideo}, output, false); });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        MediaInfo joined;
        QVERIFY(MediaProbe::probe("ffprobe", output, &joined));
        QVERIFY2(std::fabs(joined.duration - 4.0) < 0.1, qPrintable(QString::number(joined.duration)));
        QVERIFY(joined.hasAudio);
        QCOMPARE(countFrames(output), 120);
        // asked for the camera's data where there is none, the join still works
        error = join([&](VideoJoinJob &job) { job.startCopy({m_sourceVideo, m_sourceVideo}, output, true); });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(countFrames(output), 120);
    }

    // A Matroska file and an MP4 of the same video keep their times in other clocks (1/1000,
    // 1/15360): ffmpeg's concat made the join sixteen times too long after the first part.
    // The own engine copies them right; the ffmpeg program is told to encode them.
    void joinMatroskaWithMp4()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        const QString mkv = m_dir.filePath("same-video.mkv");
        const FfmpegTools::RunResult made = FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-i",
                                                             m_sourceVideo, "-c", "copy", mkv}, 60000);
        QVERIFY(made.started && made.exitCode == 0);
        VideoJoin::Part a{mkv, MediaInfo()};
        VideoJoin::Part b{m_sourceVideo, MediaInfo()};
        QVERIFY(MediaProbe::probe("ffprobe", a.path, &a.info));
        QVERIFY(MediaProbe::probe("ffprobe", b.path, &b.info));
        QVERIFY(VideoJoin::whyNot({a, b}).isEmpty());
        QCOMPARE(a.info.videoTimeBase, QString("1/1000"));
        QVERIFY(!VideoJoin::whyNotByProgram({a, b}).isEmpty());
        QVERIFY(VideoJoin::whyNotByProgram({b, b}).isEmpty());
        if (m_engine != ExportJob::OwnEngine)
            {
                QSKIP("the ffmpeg program encodes these (joinOfDifferentKindsEncodes)");
            }
        for (const QString &output : {m_dir.filePath("mkv-mp4.mkv"), m_dir.filePath("mkv-mp4.mp4")})
            {
                const QString error = join([&](VideoJoinJob &job) { job.startCopy({a.path, b.path, a.path}, output, false); });
                QVERIFY2(error.isEmpty(), qPrintable(error));
                MediaInfo joined;
                QVERIFY(MediaProbe::probe("ffprobe", output, &joined));
                QVERIFY2(std::fabs(joined.duration - 6.0) < 0.15, qPrintable(QString("%1: %2").arg(output).arg(joined.duration)));
                QCOMPARE(countFrames(output), 180);
            }
    }

    void joinOfDifferentKindsEncodes()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        // another camera: wider, 25 frames a second and no sound
        const QString other = m_dir.filePath("other-kind.mp4");
        const FfmpegTools::RunResult made = FfmpegTools::run("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                                             "color=c=red:s=480x200:r=25:d=1", "-c:v", "libx264", "-pix_fmt",
                                                             "yuv420p", other}, 60000);
        QVERIFY(made.started && made.exitCode == 0);
        VideoJoin::Part a{m_sourceVideo, MediaInfo()};
        VideoJoin::Part b{other, MediaInfo()};
        QVERIFY(MediaProbe::probe("ffprobe", a.path, &a.info));
        QVERIFY(MediaProbe::probe("ffprobe", b.path, &b.info));
        QVERIFY(!VideoJoin::whyNot({a, b}).isEmpty());
        ExportSettings settings;
        settings.encoder = "libx264";
        settings.rateControl = ExportSettings::Quality;
        settings.speed = ExportSettings::Fast;
        const QString output = m_dir.filePath("encoded-join.mp4");
        double progressed = -1.0;
        const QString error = join([&](VideoJoinJob &job)
        {
            job.startEncode({a, b}, output, VideoEncoders::byId("libx264"), settings);
        }, &progressed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(progressed > 2.5, qPrintable(QString::number(progressed)));
        MediaInfo joined;
        QVERIFY(MediaProbe::probe("ffprobe", output, &joined));
        // the first one's size and rate throughout; sound all the way, silence for the second
        QCOMPARE(joined.size, a.info.size);
        QCOMPARE(joined.fps(), 30.0);
        QVERIFY(joined.hasAudio);
        QVERIFY2(std::fabs(joined.duration - 3.0) < 0.15, qPrintable(QString::number(joined.duration)));
        QCOMPARE(countFrames(output), 90);
        QCOMPARE(VideoJoin::progressSeconds("frame=1\nout_time_us=1500000\nprogress=continue\nout_time_us=2500000\n"), 2.5);
        QCOMPARE(VideoJoin::progressSeconds("nothing"), -1.0);
    }

    void cameraDataIsKeptWhenEveryPartHasIt()
    {
        MediaInfo plain;
        QVERIFY(MediaProbe::parse(R"({"streams":[{"codec_type":"video","codec_name":"h264","width":8,"height":8,"avg_frame_rate":"30/1"},
                                     {"codec_type":"data","codec_name":"bin_data","codec_tag_string":"gpmd"}],"format":{"duration":"1"}})",
                                  &plain));
        QVERIFY(plain.hasCameraData);
        VideoJoin::Part gopro{"GX010001.MP4", plain};
        VideoJoin::Part other = gopro;
        other.info.hasCameraData = false;
        QVERIFY(VideoJoin::keepsCameraData({gopro, gopro}, "joined.mp4"));
        QVERIFY(VideoJoin::keepsCameraData({gopro, gopro}, "joined.MOV"));
        QVERIFY(!VideoJoin::keepsCameraData({gopro, other}, "joined.mp4"));
        QVERIFY(!VideoJoin::keepsCameraData({gopro, gopro}, "joined.mkv"));
        QVERIFY(VideoJoin::arguments("list", "out.mp4", true).contains("0:d?"));
        QVERIFY(!VideoJoin::arguments("list", "out.mp4").contains("0:d?"));
    }

    void canceledJoinLeavesNothing()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        VideoJoin::Part part{m_sourceVideo, MediaInfo()};
        QVERIFY(MediaProbe::probe("ffprobe", part.path, &part.info));
        ExportSettings settings;
        settings.encoder = "libx264";
        const QString output = m_dir.filePath("canceled-join.mp4");
        VideoJoinJob job(m_engine);
        QSignalSpy finished(&job, &VideoJoinJob::finished);
        job.startEncode({part, part, part, part}, output, VideoEncoders::byId("libx264"), settings);
        QVERIFY(job.isRunning());
        QTest::qWait(100);
        job.cancel();
        QVERIFY(!job.isRunning());
        QTest::qWait(300);
        QVERIFY(finished.isEmpty());
        QVERIFY(!QFile::exists(output));
    }

    // Optional: RTV_GOPRO_SAMPLE=/path/to/gopro.mp4 joins the camera file with itself and reads
    // the data of the join.
    void goproJoinKeepsItsData()
    {
        const QString sample = qEnvironmentVariable("RTV_GOPRO_SAMPLE");
        if (sample.isEmpty() || m_sourceVideo.isEmpty())
            {
                QSKIP("RTV_GOPRO_SAMPLE is not set, or ffmpeg is not installed");
            }
        VideoJoin::Part part{sample, MediaInfo()};
        QVERIFY(MediaProbe::probe("ffprobe", sample, &part.info));
        QVERIFY(part.info.hasCameraData);
        const QString output = m_dir.filePath("gopro-joined.mp4");
        QVERIFY(VideoJoin::keepsCameraData({part, part}, output));
        QString error = join([&](VideoJoinJob &job) { job.startCopy({sample, sample}, output, true); });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        MediaInfo joined;
        QVERIFY(MediaProbe::probe("ffprobe", output, &joined));
        QVERIFY(joined.hasCameraData);
        const TelemetrySessionPtr one = GpmfImporter::load(sample, &error);
        const TelemetrySessionPtr both = GpmfImporter::load(output, &error);
        QVERIFY2(one && both, qPrintable(error));
        // whatever the camera recorded (a sample without a GPS fix has no positions): twice
        const QStringList ids = one->channelIds();
        QVERIFY2(!ids.isEmpty(), "the sample has no data");
        QCOMPARE(both->channelIds(), ids);
        for (const QString &id : ids)
            {
                QVERIFY2(both->channel(id)->count() >= 2 * one->channel(id)->count() - 2, qPrintable(id));
            }
        QVERIFY(std::fabs(both->duration() - 2.0 * one->duration()) < 1.0);
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
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
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
        if (m_engine == ExportJob::OwnEngine)
            {
                QSKIP("about the ffmpeg program");
            }
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
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
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
        if (!slowTests())
            {
                QSKIP("Takes about 20 s; set RTV_SLOW_TESTS=1 to run it");
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
        if (!slowTests())
            {
                QSKIP("Takes about 20 s; set RTV_SLOW_TESTS=1 to run it");
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
        ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
        QSignalSpy finished(&job, &ExportJob::finished);
        job.stop();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<ExportJob::Outcome>(), ExportJob::Canceled);
        QVERIFY(!QFile::exists(output));
    }

    void destroyingARunningJobCleansUp()
    {
        if (m_sourceVideo.isEmpty())
            {
                QSKIP("ffmpeg is not installed");
            }
        if (!slowTests())
            {
                QSKIP("Takes about 20 s; set RTV_SLOW_TESTS=1 to run it");
            }
        const QString source = longSource(false);
        QVERIFY(!source.isEmpty());
        MediaInfo info;
        QVERIFY(MediaProbe::probe("ffprobe", source, &info));
        const QString output = m_dir.filePath("destroyed.mp4");
        const Overlay overlay = oneGauge();
        const ExportPlan plan = ExportPlan::make(source, info, settingsFor(output), overlay);
        {
            ExportJob job("ffmpeg", plan, overlay, 0.0, nullptr, m_engine);
            QSignalSpy progress(&job, &ExportJob::progress);
            job.start();
            QVERIFY(progress.wait(30000));
        }
        QVERIFY(!QFile::exists(output));
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QTEST_SET_MAIN_SOURCE_PATH
    TestExport program(ExportJob::ProgramEngine);
    int failed = QTest::qExec(&program, argc, argv);
    if (Libav::available())
        {
            TestExport own(ExportJob::OwnEngine);
            failed += QTest::qExec(&own, argc, argv);
        }
    return failed;
}

#include "test_export.moc"
