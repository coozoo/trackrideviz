#include "videoencoders.h"

#include "ffmpegtools.h"

namespace
{
VideoEncoder make(const char *id, const QString &label, bool hardware, bool hevc)
{
    VideoEncoder encoder;
    encoder.id = id;
    encoder.label = label;
    encoder.hardware = hardware;
    encoder.hevc = hevc;
    // roughly the same picture: HEVC needs a higher number than H.264, and the
    // fixed-QP modes of graphics cards a higher one than x264's CRF
    encoder.defaultQuality = (hevc ? 26 : 23) + (hardware ? 3 : 0);
    return encoder;
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

    VideoEncoder x264 = make("libx264", tr("H.264 (software, x264)"), false, false);
    x264.hasSpeed = true;
    list.append(x264);
    VideoEncoder x265 = make("libx265", tr("H.265 / HEVC (software, x265)"), false, true);
    x265.hasSpeed = true;
    list.append(x265);

    VideoEncoder h264Vaapi = make("h264_vaapi", tr("H.264 (graphics card, VA-API)"), true, false);
    h264Vaapi.vaapi = true;
    list.append(h264Vaapi);
    VideoEncoder hevcVaapi = make("hevc_vaapi", tr("H.265 / HEVC (graphics card, VA-API)"), true, true);
    hevcVaapi.vaapi = true;
    list.append(hevcVaapi);

    list.append(make("h264_nvenc", tr("H.264 (graphics card, NVIDIA)"), true, false));
    list.append(make("hevc_nvenc", tr("H.265 / HEVC (graphics card, NVIDIA)"), true, true));
    list.append(make("h264_qsv", tr("H.264 (graphics card, Intel Quick Sync)"), true, false));
    list.append(make("hevc_qsv", tr("H.265 / HEVC (graphics card, Intel Quick Sync)"), true, true));
    list.append(make("h264_amf", tr("H.264 (graphics card, AMD)"), true, false));
    list.append(make("hevc_amf", tr("H.265 / HEVC (graphics card, AMD)"), true, true));

    // VideoToolbox has constant quality only on some Macs; bitrate works on all
    VideoEncoder h264Vt = make("h264_videotoolbox", tr("H.264 (Apple VideoToolbox)"), true, false);
    h264Vt.hasQuality = false;
    list.append(h264Vt);
    VideoEncoder hevcVt = make("hevc_videotoolbox", tr("H.265 / HEVC (Apple VideoToolbox)"), true, true);
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
            args << "-preset" << speedPreset(settings.speed);
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
    for (const VideoEncoder &encoder : known())
        {
            // lines look like " V....D libx264              libx264 H.264 ..."
            if (!compiledIn.contains(" " + encoder.id + " "))
                {
                    continue;
                }
            if (!encoder.hardware)
                {
                    usable.append(encoder);
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
            const FfmpegTools::RunResult test = FfmpegTools::run(ffmpegProgram, args, 10000);
            if (test.started && test.exitCode == 0)
                {
                    usable.append(encoder);
                }
        }
    return usable;
}
