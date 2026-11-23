#include "exportplan.h"

#include "../overlay/overlay.h"
#include "soundtrack.h"
#include "videoencoders.h"

#include <QFileInfo>
#include <QSysInfo>

#include <algorithm>
#include <cmath>

namespace
{
// The sound encoded again as chosen; AAC when it was to be copied but cannot be.
QStringList audioEncoding(const ExportSettings &settings)
{
    const QString bitrate = QString("%1k").arg(settings.audioKbps);
    switch (settings.audio)
        {
        case ExportSettings::OpusAudio:
            // Opus takes no more than 256 kbit/s a channel: mono sound refuses the 384 of a YouTube upload
            return {"-c:a", "libopus", "-b:a", QString("%1k").arg(std::min(settings.audioKbps, 256))};
        case ExportSettings::VorbisAudio:
            return {"-c:a", "libvorbis", "-b:a", bitrate};
        case ExportSettings::AacAudio:
        case ExportSettings::CopyAudio:
        case ExportSettings::NoAudio:
            break;
        }
    return {"-c:a", "aac", "-b:a", bitrate};
}

// With no video: a picture of the background's color, of the plan's size and rate.
QString background(const ExportSettings &settings, const ExportPlan &plan)
{
    return QString("color=c=0x%1:s=%2x%3:r=%4/%5").arg(settings.background.name().mid(1))
           .arg(plan.outputSize.width()).arg(plan.outputSize.height()).arg(plan.fpsNum).arg(plan.fpsDen);
}
}

QSize ExportPlan::outputSizeFor(const QSize &source, int wantedHeight)
{
    auto even = [](double value)
    {
        return std::max(2, int(std::lround(value / 2.0)) * 2);
    };
    if (wantedHeight <= 0 || wantedHeight >= source.height())
        {
            return QSize(even(source.width()), even(source.height()));
        }
    return QSize(even(double(source.width()) * wantedHeight / source.height()), even(wantedHeight));
}

ExportPlan ExportPlan::make(const QString &videoPath, const MediaInfo &info,
                            const ExportSettings &settings, const Overlay &overlay,
                            const QVector<TimeRange> &cuts, const Soundtrack &soundtrack)
{
    ExportPlan plan;
    plan.outputSize = outputSizeFor(info.size, settings.height);
    plan.overlayRect = overlay.boundingRect(plan.outputSize);
    plan.regions = overlay.regions(plan.outputSize);
    for (const QRect &region : plan.regions)
        {
            plan.stackTops.append(plan.stackSize.height());
            plan.stackSize = QSize(std::max(plan.stackSize.width(), region.width()), plan.stackSize.height() + region.height());
        }
    plan.fpsNum = info.fpsNum;
    plan.fpsDen = info.fpsDen;
    plan.speedUp = settings.isSpedUp() ? settings.speedUp : 1.0;
    const bool spedUp = plan.speedUp > 1.0;
    plan.startTime = std::clamp(settings.start, 0.0, std::max(0.0, info.duration));
    const double available = std::max(0.0, info.duration - plan.startTime);
    const double length = settings.duration > 0.0 ? std::min(settings.duration, available) : available;
    const QVector<TimeRange> kept = CutList::kept(cuts, plan.startTime, plan.startTime + length);
    // only when a cut falls inside what is exported
    if (kept.size() != 1 || kept.first() != TimeRange{plan.startTime, plan.startTime + length})
        {
            plan.kept = kept;
        }
    const bool cut = !plan.kept.isEmpty();
    // what is kept of the video, then how long that plays
    const double keptLength = cut ? CutList::keptLength(plan.kept) : length;
    const double outputLength = keptLength / plan.speedUp;
    const bool piped = !plan.regions.isEmpty();
    // The output is as long as the shorter of the video and the pictures.
    // The exact count is used when the container gives it; otherwise the
    // estimate is rounded down, because one picture too many would be written
    // into a pipe ffmpeg has already left.
    const bool wholeVideo = plan.startTime <= 0.0 && settings.duration <= 0.0 && !cut && !spedUp;
    const qint64 estimated = std::max<qint64>(1, qint64(std::floor(outputLength * info.fps() + 1e-6)));
    plan.frameCount = !piped ? 0 : wholeVideo && info.frameCount > 0 ? info.frameCount : estimated;
    plan.outputPath = settings.outputPath;
    // the gauges alone: no camera picture and no sound, the gauges on nothing
    const bool gaugesOnly = settings.picture == ExportSettings::GaugesOnly;
    plan.transparent = gaugesOnly;
    // the video's own sound, unless turned all the way down or sped up, and the music
    const bool videoSound = !gaugesOnly && !spedUp && info.hasAudio && soundtrack.videoGain() > 0.0;
    const bool music = !gaugesOnly && soundtrack.hasMusic() && settings.audio != ExportSettings::NoAudio;
    plan.hasAudio = settings.audio != ExportSettings::NoAudio && (videoSound || music);
    // made anew from the sound and the music, at their volumes
    const bool mixed = plan.hasAudio && soundtrack.changesSound();

    const VideoEncoder encoder = VideoEncoders::byId(settings.encoder);
    // no video: the gauges over a plain color (VideoBackground), as long as asked for
    const bool noVideo = videoPath.isEmpty();
    if (!gaugesOnly)
        {
            plan.globalOptions << VideoEncoders::globalArguments(encoder);
        }
    if (!gaugesOnly && !noVideo)
        {
            // input 0: the video, cut on the input side so nothing outside the range is decoded
            Input video{{}, videoPath};
            if (plan.startTime > 0.0)
                {
                    video.options << "-ss" << QString::number(plan.startTime, 'f', 3);
                }
            if (settings.duration > 0.0)
                {
                    video.options << "-t" << QString::number(length, 'f', 3);
                }
            plan.inputs.append(video);
        }

    // input 1 (0 for the gauges alone): the gauges from the application
    if (piped)
        {
            // QImage::Format_ARGB32 is 0xAARRGGBB per pixel: B, G, R, A in memory on
            // little-endian machines, A, R, G, B on big-endian ones
            const char *pixelFormat = QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "bgra" : "argb";
            plan.inputs.append({{"-f", "rawvideo", "-pixel_format", pixelFormat,
                                 "-video_size", QString("%1x%2").arg(plan.stackSize.width()).arg(plan.stackSize.height()),
                                 "-framerate", QString("%1/%2").arg(plan.fpsNum).arg(plan.fpsDen)}, Pipe});
        }
    // then the music: inputs 2, 3, ... (one fewer without gauges, one fewer without a video)
    const int firstMusicInput = (noVideo ? 0 : 1) + (piped ? 1 : 0);
    if (music)
        {
            for (const MusicTrack &track : soundtrack.tracks)
                {
                    if (track.plays())
                        {
                            plan.inputs.append({{}, track.path});
                        }
                }
        }

    // the kept parts, by time from the first frame read (the input starts at startTime)
    QString keptExpression;
    for (const TimeRange &part : plan.kept)
        {
            keptExpression += QString("%1gte(t\\,%2)*lt(t\\,%3)").arg(keptExpression.isEmpty() ? "" : "+")
                              .arg(part.start - plan.startTime, 0, 'f', 3).arg(part.end - plan.startTime, 0, 'f', 3);
        }
    QStringList chain;
    if (cut)
        {
            // what is left closes up: timestamps counted again from the kept frames
            chain << QString("select=%1").arg(keptExpression) << "setpts=N/FRAME_RATE/TB";
        }
    if (spedUp)
        {
            // played faster, then at the video's own rate again: the frames in between are left out
            const QString factor = QString::number(plan.speedUp, 'g', 9);
            chain << (cut ? QString("setpts=PTS/%1").arg(factor) : QString("setpts=(PTS-STARTPTS)/%1").arg(factor))
                  << QString("fps=%1/%2").arg(plan.fpsNum).arg(plan.fpsDen);
        }
    if (plan.outputSize != info.size)
        {
            chain << QString("scale=%1:%2").arg(plan.outputSize.width()).arg(plan.outputSize.height());
        }
    QString graph;
    if (piped)
        {
            // QPainter produces premultiplied alpha; the output ends when the pictures do.
            // Over nothing, overlay keeps the colors right only from straight alpha, which
            // the job then sends: premultiplied, it darkens every edge that is see-through.
            const QString alpha = gaugesOnly ? "straight:format=auto" : "premultiplied";
            const bool overNothing = gaugesOnly || noVideo;
            auto overlayAt = [&alpha](const QRect &region)
            {
                return QString("overlay=%1:%2:alpha=%3:eof_action=endall").arg(region.x()).arg(region.y()).arg(alpha);
            };
            const QString gauges = overNothing ? "[0:v]" : "[1:v]";
            const QString finish = gaugesOnly ? QString() : "," + VideoEncoders::outputFilter(encoder);
            QStringList parts;
            QString video = "[0:v]";
            if (gaugesOnly)
                {
                    parts << QString("color=c=black@0:s=%1x%2:r=%3/%4,format=rgba[base]")
                          .arg(plan.outputSize.width()).arg(plan.outputSize.height()).arg(plan.fpsNum).arg(plan.fpsDen);
                    video = "[base]";
                }
            else if (noVideo)
                {
                    parts << background(settings, plan) + "[base]";
                    video = "[base]";
                }
            else if (!chain.isEmpty())
                {
                    parts << QString("[0:v]%1[base]").arg(chain.join(','));
                    video = "[base]";
                }
            const int count = int(plan.regions.size());
            if (count == 1)
                {
                    parts << QString("%1%2%3%4[v]").arg(video, gauges, overlayAt(plan.regions.first()), finish);
                }
            else
                {
                    // cut the stacked picture back into its regions
                    QString split = QString("%1split=%2").arg(gauges).arg(count);
                    for (int i = 0; i < count; ++i)
                        {
                            split += QString("[s%1]").arg(i);
                        }
                    parts << split;
                    for (int i = 0; i < count; ++i)
                        {
                            const QRect &region = plan.regions.at(i);
                            parts << QString("[s%1]crop=%2:%3:0:%4[g%1]").arg(i).arg(region.width()).arg(region.height()).arg(plan.stackTops.at(i));
                        }
                    for (int i = 0; i < count; ++i)
                        {
                            const QString out = i == count - 1 ? finish + "[v]" : QString("[o%1]").arg(i);
                            parts << QString("%1[g%2]%3%4").arg(video).arg(i).arg(overlayAt(plan.regions.at(i)), out);
                            video = QString("[o%1]").arg(i);
                        }
                }
            graph = parts.join(';');
        }
    else if (noVideo)
        {
            // nothing to draw: the color alone, as long as asked for
            graph = QString("%1:d=%2,%3[v]").arg(background(settings, plan)).arg(outputLength, 0, 'f', 3)
                    .arg(VideoEncoders::outputFilter(encoder));
        }
    else
        {
            chain << VideoEncoders::outputFilter(encoder);
            graph = QString("[0:v]%1[v]").arg(chain.join(','));
        }
    // sound cut the same way can not be copied
    const bool cutAudio = cut && plan.hasAudio && videoSound;
    // the video's sound so far: the input, or what is left of it after the cuts
    QString videoAudio = "[0:a]";
    const QString cutOut = mixed ? "[ac]" : "[a]";
    if (cutAudio)
        {
            // each kept part trimmed out and the parts put end to end: exact to the
            // sample, and aselect lets everything through in some ffmpeg releases (8.0)
            const int count = int(plan.kept.size());
            auto trimmed = [&](int i)
            {
                const TimeRange &part = plan.kept.at(i);
                return QString("atrim=start=%1:end=%2,asetpts=PTS-STARTPTS")
                       .arg(part.start - plan.startTime, 0, 'f', 3).arg(part.end - plan.startTime, 0, 'f', 3);
            };
            if (count == 1)
                {
                    graph += QString(";[0:a]%1%2").arg(trimmed(0), cutOut);
                }
            else
                {
                    QString split = QString(";[0:a]asplit=%1").arg(count);
                    QString parts;
                    QString joined;
                    for (int i = 0; i < count; ++i)
                        {
                            split += QString("[ai%1]").arg(i);
                            parts += QString(";[ai%1]%2[ak%1]").arg(i).arg(trimmed(i));
                            joined += QString("[ak%1]").arg(i);
                        }
                    graph += split + parts + QString(";%1concat=n=%2:v=0:a=1%3").arg(joined).arg(count).arg(cutOut);
                }
            videoAudio = cutOut;
        }
    // made louder than recorded: the peaks held under full scale rather than cut off
    const bool boosted = soundtrack.videoGain() > 1.0 || (music && soundtrack.musicGain() > 1.0);
    const QString mixOut = boosted ? "[am]" : "[a]";
    if (mixed)
        {
            // what the music needs at the start of the export: when it is still to come, how
            // long to wait; when it has begun before, how much of it is gone
            const QString musicGraph = music ? SoundtrackMix::music(soundtrack, firstMusicInput,
                                       -soundtrack.musicTime(plan.startTime, cuts), outputLength, "[mus]") : QString();
            if (videoSound)
                {
                    graph += QString(";%1volume=%2,aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo%3")
                             .arg(videoAudio).arg(soundtrack.videoGain(), 0, 'f', 3).arg(musicGraph.isEmpty() ? mixOut : "[vs]");
                    if (!musicGraph.isEmpty() && soundtrack.ducking)
                        {
                            // the camera's sound, as loud as it is set, presses the music down
                            // while it is loud (about 10 dB at a usual level; a quiet camera not
                            // at all) and lets it back up within a second
                            graph += ";[vs]asplit=2[vsm][vsk];" + musicGraph
                                     + ";[mus][vsk]sidechaincompress=threshold=0.02:ratio=6:attack=50:release=800[mud]"
                                     ";[vsm][mud]amix=inputs=2:duration=first:dropout_transition=0:normalize=0" + mixOut;
                        }
                    else if (!musicGraph.isEmpty())
                        {
                            // as long as the video's sound; both at the volume set, not halved
                            graph += ";" + musicGraph + ";[vs][mus]amix=inputs=2:duration=first:dropout_transition=0:normalize=0" + mixOut;
                        }
                }
            else
                {
                    graph += ";" + QString(musicGraph).replace("[mus]", mixOut);
                }
            if (boosted)
                {
                    graph += ";[am]alimiter=limit=0.97:level=0[a]";
                }
        }
    plan.filterGraph = graph;

    if (mixed || cutAudio)
        {
            plan.audioMap = "[a]";
            plan.audioOptions = audioEncoding(settings);
        }
    else if (plan.hasAudio)
        {
            switch (settings.audio)
                {
                case ExportSettings::CopyAudio:
                    // "?": a video without sound is not an error
                    plan.audioMap = "0:a?";
                    plan.audioOptions = QStringList{"-c:a", "copy"};
                    break;
                case ExportSettings::NoAudio:
                    break;
                case ExportSettings::AacAudio:
                case ExportSettings::OpusAudio:
                case ExportSettings::VorbisAudio:
                    plan.audioMap = "0:a?";
                    plan.audioOptions = audioEncoding(settings);
                    break;
                }
        }

    const QString suffix = QFileInfo(settings.outputPath).suffix().toLower();
    if (gaugesOnly && ExportSettings::isVp9Alpha(suffix))
        {
            // constant quality: mostly empty pictures take little whatever the size
            plan.videoOptions << "-c:v" << "libvpx-vp9" << "-pix_fmt" << "yuva420p" << "-crf" << "30" << "-b:v" << "0"
                              << "-row-mt" << "1" << "-deadline" << "good" << "-cpu-used" << "4"
                              // libvpx loses the transparency with alternate reference frames
                              << "-auto-alt-ref" << "0";
        }
    else if (gaugesOnly)
        {
            // what video editors take with transparency; big, but exact
            plan.videoOptions << "-c:v" << "prores_ks" << "-profile:v" << "4444" << "-pix_fmt" << "yuva444p10le";
        }
    else
        {
            plan.videoOptions << VideoEncoders::arguments(encoder, settings);
        }
    const bool mp4Family = ExportSettings::isMp4Family(suffix);
    if (!gaugesOnly && encoder.codec == VideoEncoder::Hevc && mp4Family)
        {
            // without this tag Apple players refuse HEVC in MP4
            plan.muxerArguments << "-tag:v" << "hvc1";
        }
    if (mp4Family)
        {
            // index at the front: the file starts playing before it is fully downloaded
            plan.muxerArguments << "-movflags" << "+faststart";
        }
    plan.arguments = plan.ffmpegArguments();
    return plan;
}

QStringList ExportPlan::ffmpegArguments() const
{
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y"};
    args << globalOptions;
    for (const Input &input : inputs)
        {
            args << input.options << "-i" << input.source;
        }
    args << "-filter_complex" << filterGraph << "-map" << "[v]";
    if (audioMap.isEmpty())
        {
            args << "-an";
        }
    else
        {
            args << "-map" << audioMap << audioOptions;
        }
    args << videoOptions << muxerArguments << outputPath;
    return args;
}
