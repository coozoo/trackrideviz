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
            return {"-c:a", "libopus", "-b:a", bitrate};
        case ExportSettings::VorbisAudio:
            return {"-c:a", "libvorbis", "-b:a", bitrate};
        case ExportSettings::AacAudio:
        case ExportSettings::CopyAudio:
        case ExportSettings::NoAudio:
            break;
        }
    return {"-c:a", "aac", "-b:a", bitrate};
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
    const double outputLength = cut ? CutList::keptLength(plan.kept) : length;
    const bool piped = !plan.regions.isEmpty();
    // The output is as long as the shorter of the video and the pictures.
    // The exact count is used when the container gives it; otherwise the
    // estimate is rounded down, because one picture too many would be written
    // into a pipe ffmpeg has already left.
    const bool wholeVideo = plan.startTime <= 0.0 && settings.duration <= 0.0 && !cut;
    const qint64 estimated = std::max<qint64>(1, qint64(std::floor(outputLength * info.fps() + 1e-6)));
    plan.frameCount = !piped ? 0 : wholeVideo && info.frameCount > 0 ? info.frameCount : estimated;
    plan.outputPath = settings.outputPath;
    // the gauges alone: no camera picture and no sound, the gauges on nothing
    const bool gaugesOnly = settings.picture == ExportSettings::GaugesOnly;
    plan.transparent = gaugesOnly;
    // the video's own sound, unless turned all the way down, and the music
    const bool videoSound = !gaugesOnly && info.hasAudio && soundtrack.videoGain() > 0.0;
    const bool music = !gaugesOnly && soundtrack.hasMusic() && settings.audio != ExportSettings::NoAudio;
    plan.hasAudio = settings.audio != ExportSettings::NoAudio && (videoSound || music);
    // made anew from the sound and the music, at their volumes
    const bool mixed = plan.hasAudio && soundtrack.changesSound();

    const VideoEncoder encoder = VideoEncoders::byId(settings.encoder);
    QStringList &args = plan.arguments;
    args << "-hide_banner" << "-loglevel" << "error" << "-y";
    if (!gaugesOnly)
        {
            args << VideoEncoders::globalArguments(encoder);
            // input 0: the video, cut on the input side so nothing outside the range is decoded
            if (plan.startTime > 0.0)
                {
                    args << "-ss" << QString::number(plan.startTime, 'f', 3);
                }
            if (settings.duration > 0.0)
                {
                    args << "-t" << QString::number(length, 'f', 3);
                }
            args << "-i" << videoPath;
        }

    // input 1 (0 for the gauges alone): the gauges from the application
    if (piped)
        {
            // QImage::Format_ARGB32 is 0xAARRGGBB per pixel: B, G, R, A in memory on
            // little-endian machines, A, R, G, B on big-endian ones
            const char *pixelFormat = QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "bgra" : "argb";
            args << "-f" << "rawvideo" << "-pixel_format" << pixelFormat
                 << "-video_size" << QString("%1x%2").arg(plan.stackSize.width()).arg(plan.stackSize.height())
                 << "-framerate" << QString("%1/%2").arg(plan.fpsNum).arg(plan.fpsDen)
                 << "-i" << "pipe:0";
        }
    // inputs 2, 3, ... (1, 2, ... without gauges): the music
    const int firstMusicInput = piped ? 2 : 1;
    if (music)
        {
            for (const MusicTrack &track : soundtrack.tracks)
                {
                    if (track.plays())
                        {
                            args << "-i" << track.path;
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
            auto overlayAt = [&alpha](const QRect &region)
            {
                return QString("overlay=%1:%2:alpha=%3:eof_action=endall").arg(region.x()).arg(region.y()).arg(alpha);
            };
            const QString gauges = gaugesOnly ? "[0:v]" : "[1:v]";
            const QString finish = gaugesOnly ? QString() : "," + VideoEncoders::outputFilter(encoder);
            QStringList parts;
            QString video = "[0:v]";
            if (gaugesOnly)
                {
                    parts << QString("color=c=black@0:s=%1x%2:r=%3/%4,format=rgba[base]")
                          .arg(plan.outputSize.width()).arg(plan.outputSize.height()).arg(plan.fpsNum).arg(plan.fpsDen);
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
    args << "-filter_complex" << graph << "-map" << "[v]";

    if (mixed || cutAudio)
        {
            args << "-map" << "[a]" << audioEncoding(settings);
        }
    else if (!plan.hasAudio)
        {
            args << "-an";
        }
    else switch (settings.audio)
        {
        case ExportSettings::CopyAudio:
            // "?": a video without sound is not an error
            args << "-map" << "0:a?" << "-c:a" << "copy";
            break;
        case ExportSettings::NoAudio:
            args << "-an";
            break;
        case ExportSettings::AacAudio:
        case ExportSettings::OpusAudio:
        case ExportSettings::VorbisAudio:
            args << "-map" << "0:a?" << audioEncoding(settings);
            break;
        }

    const QString suffix = QFileInfo(settings.outputPath).suffix().toLower();
    if (gaugesOnly && ExportSettings::isVp9Alpha(suffix))
        {
            // constant quality: mostly empty pictures take little whatever the size
            args << "-c:v" << "libvpx-vp9" << "-pix_fmt" << "yuva420p" << "-crf" << "30" << "-b:v" << "0"
                 << "-row-mt" << "1" << "-deadline" << "good" << "-cpu-used" << "4"
                 // libvpx loses the transparency with alternate reference frames
                 << "-auto-alt-ref" << "0";
        }
    else if (gaugesOnly)
        {
            // what video editors take with transparency; big, but exact
            args << "-c:v" << "prores_ks" << "-profile:v" << "4444" << "-pix_fmt" << "yuva444p10le";
        }
    else
        {
            args << VideoEncoders::arguments(encoder, settings);
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
    args << plan.muxerArguments;
    args << settings.outputPath;
    return plan;
}
