#include "videoencoders.h"

#include "ffmpegtools.h"
#include "libav.h"

#include <QHash>
#include <QMutex>

#include <future>
#include <vector>

namespace
{
VideoEncoder make(const char *id, const QString &label, bool hardware, VideoEncoder::Codec codec)
{
    VideoEncoder encoder;
    encoder.id = id;
    encoder.label = label;
    encoder.hardware = hardware;
    encoder.codec = codec;
    // roughly the same picture: HEVC needs a higher number than H.264, AV1's scale goes to
    // 63, and the fixed-QP modes of graphics cards want a higher one than x264's CRF
    encoder.defaultQuality = (codec == VideoEncoder::Av1 ? 32 : codec == VideoEncoder::Hevc ? 26 : 23) + (hardware ? 3 : 0);
    if (hardware && codec == VideoEncoder::Av1)
        {
            // each card's own scale, not tried here: the bitrate means the same on all
            encoder.hasQuality = false;
        }
    return encoder;
}

// SVT-AV1 counts its speed from 0, the slowest, to 13
const char *svtPreset(ExportSettings::Speed speed)
{
    switch (speed)
        {
        case ExportSettings::Fast:
            return "10";
        case ExportSettings::Small:
            return "5";
        default:
            return "8";
        }
}

const char *speedPreset(ExportSettings::Speed speed)
{
    switch (speed)
        {
        case ExportSettings::Fast:
            return "veryfast";
        case ExportSettings::Small:
            return "slow";
        default:
            return "medium";
        }
}
}

QVector<VideoEncoder> VideoEncoders::known()
{
    QVector<VideoEncoder> list;

    VideoEncoder x264 = make("libx264", tr("H.264 (software, x264)"), false, VideoEncoder::H264);
    x264.hasSpeed = true;
    list.append(x264);
    VideoEncoder x265 = make("libx265", tr("H.265 / HEVC (software, x265)"), false, VideoEncoder::Hevc);
    x265.hasSpeed = true;
    list.append(x265);
    // the smallest files for the picture, and the slowest to make
    VideoEncoder svt = make("libsvtav1", tr("AV1 (software, SVT-AV1)"), false, VideoEncoder::Av1);
    svt.hasSpeed = true;
    list.append(svt);

    VideoEncoder h264Vaapi = make("h264_vaapi", tr("H.264 (graphics card, VA-API)"), true, VideoEncoder::H264);
    h264Vaapi.vaapi = true;
    list.append(h264Vaapi);
    VideoEncoder hevcVaapi = make("hevc_vaapi", tr("H.265 / HEVC (graphics card, VA-API)"), true, VideoEncoder::Hevc);
    hevcVaapi.vaapi = true;
    list.append(hevcVaapi);
    VideoEncoder av1Vaapi = make("av1_vaapi", tr("AV1 (graphics card, VA-API)"), true, VideoEncoder::Av1);
    av1Vaapi.vaapi = true;
    list.append(av1Vaapi);

    list.append(make("h264_nvenc", tr("H.264 (graphics card, NVIDIA)"), true, VideoEncoder::H264));
    list.append(make("hevc_nvenc", tr("H.265 / HEVC (graphics card, NVIDIA)"), true, VideoEncoder::Hevc));
    list.append(make("av1_nvenc", tr("AV1 (graphics card, NVIDIA)"), true, VideoEncoder::Av1));
    list.append(make("h264_qsv", tr("H.264 (graphics card, Intel Quick Sync)"), true, VideoEncoder::H264));
    list.append(make("hevc_qsv", tr("H.265 / HEVC (graphics card, Intel Quick Sync)"), true, VideoEncoder::Hevc));
    list.append(make("av1_qsv", tr("AV1 (graphics card, Intel Quick Sync)"), true, VideoEncoder::Av1));
    list.append(make("h264_amf", tr("H.264 (graphics card, AMD)"), true, VideoEncoder::H264));
    list.append(make("hevc_amf", tr("H.265 / HEVC (graphics card, AMD)"), true, VideoEncoder::Hevc));
    list.append(make("av1_amf", tr("AV1 (graphics card, AMD)"), true, VideoEncoder::Av1));

    // VideoToolbox has constant quality only on some Macs; bitrate works on all
    VideoEncoder h264Vt = make("h264_videotoolbox", tr("H.264 (Apple VideoToolbox)"), true, VideoEncoder::H264);
    h264Vt.hasQuality = false;
    list.append(h264Vt);
    VideoEncoder hevcVt = make("hevc_videotoolbox", tr("H.265 / HEVC (Apple VideoToolbox)"), true, VideoEncoder::Hevc);
    hevcVt.hasQuality = false;
    list.append(hevcVt);
    return list;
}

VideoEncoder VideoEncoders::byId(const QString &id)
{
    const QVector<VideoEncoder> all = known();
    for (const VideoEncoder &encoder : all)
        {
            if (encoder.id == id)
                {
                    return encoder;
                }
        }
    return all.first();
}

QStringList VideoEncoders::globalArguments(const VideoEncoder &encoder)
{
    if (encoder.vaapi)
        {
            // the first render node; machines with several cards may need a choice here later
            return {"-vaapi_device", "/dev/dri/renderD128"};
        }
    return {};
}

QString VideoEncoders::outputFilter(const VideoEncoder &encoder)
{
    if (encoder.vaapi)
        {
            return QStringLiteral("format=nv12,hwupload");
        }
    if (encoder.hardware)
        {
            return QStringLiteral("format=nv12");
        }
    // what every player can show
    return QStringLiteral("format=yuv420p");
}

QStringList VideoEncoders::arguments(const VideoEncoder &encoder, const ExportSettings &settings)
{
    QStringList args = {"-c:v", encoder.id};
    const QString bitrate = QString("%1k").arg(settings.bitrateKbps);
    const QString quality = QString::number(settings.quality);
    const bool useQuality = settings.rateControl == ExportSettings::Quality && encoder.hasQuality;

    if (encoder.id.startsWith("lib"))
        {
            args << (useQuality ? QStringList{"-crf", quality} : QStringList{"-b:v", bitrate});
        }
    else if (encoder.vaapi)
        {
            args << (useQuality ? QStringList{"-rc_mode", "CQP", "-qp", quality}
                     : QStringList{"-rc_mode", "VBR", "-b:v", bitrate});
        }
    else if (encoder.id.endsWith("_nvenc"))
        {
            args << (useQuality ? QStringList{"-rc", "vbr", "-cq", quality, "-b:v", "0"}
                     : QStringList{"-rc", "vbr", "-b:v", bitrate});
        }
    else if (encoder.id.endsWith("_qsv"))
        {
            args << (useQuality ? QStringList{"-global_quality", quality} : QStringList{"-b:v", bitrate});
        }
    else if (encoder.id.endsWith("_amf"))
        {
            args << (useQuality ? QStringList{"-rc", "cqp", "-qp_i", quality, "-qp_p", quality}
                     : QStringList{"-rc", "vbr_peak", "-b:v", bitrate});
        }
    else
        {
            args << "-b:v" << bitrate;
        }

    if (encoder.hasSpeed)
        {
            args << "-preset" << (encoder.codec == VideoEncoder::Av1 ? svtPreset(settings.speed) : speedPreset(settings.speed));
        }
    return args;
}

QVector<VideoEncoder> VideoEncoders::detect(const QString &ffmpegProgram)
{
    QVector<VideoEncoder> usable;
    const FfmpegTools::RunResult listing = FfmpegTools::run(ffmpegProgram, {"-hide_banner", "-encoders"});
    if (!listing.started || listing.exitCode != 0)
        {
            return usable;
        }
    const QString compiledIn = QString::fromLocal8Bit(listing.output);
    // a hardware encoder is only usable when the driver takes a frame: each is tried, all at
    // once, as starting ffmpeg takes a while (on Windows above all) and a test may wait long
    const QVector<VideoEncoder> all = known();
    std::vector<std::future<bool>> works(size_t(all.size()));
    for (int i = 0; i < all.size(); ++i)
        {
            const VideoEncoder &encoder = all.at(i);
            // lines look like " V....D libx264              libx264 H.264 ..."
            if (!encoder.hardware || !compiledIn.contains(" " + encoder.id + " "))
                {
                    continue;
                }
            QStringList args = {"-hide_banner", "-loglevel", "error"};
            args << globalArguments(encoder);
            args << "-f" << "lavfi" << "-i" << "color=c=black:s=320x240:r=30:d=0.2";
            args << "-vf" << outputFilter(encoder);
            // constant QP is the mode every driver has
            ExportSettings probe;
            probe.rateControl = ExportSettings::Quality;
            args << arguments(encoder, probe);
            args << "-f" << "null" << "-";
            works[size_t(i)] = std::async(std::launch::async, [ffmpegProgram, args]()
            {
                const FfmpegTools::RunResult test = FfmpegTools::run(ffmpegProgram, args, 10000);
                return test.started && test.exitCode == 0;
            });
        }
    // in the order known() gives, whichever test ends first
    for (int i = 0; i < all.size(); ++i)
        {
            const VideoEncoder &encoder = all.at(i);
            if (!compiledIn.contains(" " + encoder.id + " "))
                {
                    continue;
                }
            if (!encoder.hardware || works[size_t(i)].get())
                {
                    usable.append(encoder);
                }
        }
    return usable;
}

QVector<VideoEncoder> VideoEncoders::detectOwn()
{
    QVector<VideoEncoder> usable;
    const QVector<VideoEncoder> all = known();
    std::vector<std::future<bool>> works(size_t(all.size()));
    for (int i = 0; i < all.size(); ++i)
        {
            const VideoEncoder &encoder = all.at(i);
            if (!encoder.hardware || !Libav::hasEncoder(encoder.id))
                {
                    continue;
                }
            // constant QP is the mode every driver has
            ExportSettings probe;
            probe.rateControl = ExportSettings::Quality;
            QStringList options = arguments(encoder, probe);
            options.removeFirst();   // "-c:v"
            options.removeFirst();   // its name
            const QString device = encoder.vaapi ? globalArguments(encoder).value(1) : QString();
            works[size_t(i)] = std::async(std::launch::async, [encoder, options, device]()
            {
                return Libav::encoderWorks(encoder.id, options, device);
            });
        }
    for (int i = 0; i < all.size(); ++i)
        {
            const VideoEncoder &encoder = all.at(i);
            if (!Libav::hasEncoder(encoder.id))
                {
                    continue;
                }
            if (!encoder.hardware || works[size_t(i)].get())
                {
                    usable.append(encoder);
                }
        }
    return usable;
}

VideoEncoders::ForEngine VideoEncoders::detectFor(bool preferOwn, const QString &ffmpegProgram)
{
    // the graphics card does not change while the program runs: tried once for each engine
    static QMutex mutex;
    static QHash<QString, ForEngine> detected;
    const QString key = (preferOwn ? QStringLiteral("own:") : QStringLiteral("program:")) + ffmpegProgram;
    const QMutexLocker lock(&mutex);
    const auto known = detected.constFind(key);
    if (known != detected.constEnd())
        {
            return *known;
        }
    ForEngine result;
    result.own = preferOwn && Libav::available();
    result.encoders = result.own ? detectOwn() : detect(ffmpegProgram);
    if (result.encoders.isEmpty())
        {
            // the other engine, if it has something
            const bool other = !result.own;
            if (!other || Libav::available())
                {
                    const QVector<VideoEncoder> found = other ? detectOwn() : detect(ffmpegProgram);
                    if (!found.isEmpty())
                        {
                            result.own = other;
                            result.encoders = found;
                        }
                }
        }
    detected.insert(key, result);
    return result;
}
