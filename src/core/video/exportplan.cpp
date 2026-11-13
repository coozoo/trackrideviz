#include "exportplan.h"

#include "../overlay/overlay.h"
#include "videoencoders.h"

#include <QFileInfo>
#include <QSysInfo>

#include <algorithm>
#include <cmath>

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
                            const QVector<TimeRange> &cuts)
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
    plan.hasAudio = info.hasAudio && settings.audio != ExportSettings::NoAudio;

    const VideoEncoder encoder = VideoEncoders::byId(settings.encoder);
    QStringList &args = plan.arguments;
    args << "-hide_banner" << "-loglevel" << "error" << "-y";
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

    // input 1: the gauges from the application
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
            // QPainter produces premultiplied alpha; the output ends when the pictures do
            auto overlayAt = [](const QRect &region)
            {
                return QString("overlay=%1:%2:alpha=premultiplied:eof_action=endall").arg(region.x()).arg(region.y());
            };
            QStringList parts;
            QString video = "[0:v]";
            if (!chain.isEmpty())
                {
                    parts << QString("[0:v]%1[base]").arg(chain.join(','));
                    video = "[base]";
                }
            const int count = int(plan.regions.size());
            if (count == 1)
                {
                    parts << QString("%1[1:v]%2,%3[v]").arg(video, overlayAt(plan.regions.first()), VideoEncoders::outputFilter(encoder));
                }
            else
                {
                    // cut the stacked picture back into its regions
                    QString split = QString("[1:v]split=%1").arg(count);
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
                            const QString out = i == count - 1 ? QString(",%1[v]").arg(VideoEncoders::outputFilter(encoder))
                                                : QString("[o%1]").arg(i);
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
    const bool cutAudio = cut && plan.hasAudio;
    if (cutAudio)
        {
            graph += QString(";[0:a]aselect=%1,asetpts=N/SR/TB[a]").arg(keptExpression);
        }
    args << "-filter_complex" << graph << "-map" << "[v]";

    if (cutAudio)
        {
            args << "-map" << "[a]" << "-c:a" << "aac" << "-b:a" << "192k";
        }
    else switch (settings.audio)
        {
        case ExportSettings::CopyAudio:
            // "?": a video without sound is not an error
            args << "-map" << "0:a?" << "-c:a" << "copy";
            break;
        case ExportSettings::AacAudio:
            args << "-map" << "0:a?" << "-c:a" << "aac" << "-b:a" << "192k";
            break;
        case ExportSettings::NoAudio:
            args << "-an";
            break;
        }

    args << VideoEncoders::arguments(encoder, settings);
    const QString suffix = QFileInfo(settings.outputPath).suffix().toLower();
    const bool mp4Family = suffix == "mp4" || suffix == "mov" || suffix == "m4v";
    if (encoder.hevc && mp4Family)
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
