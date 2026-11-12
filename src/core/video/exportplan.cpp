#include "exportplan.h"

#include "../overlay/overlay.h"
#include "videoencoders.h"

#include <QFileInfo>

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
                            const ExportSettings &settings, const Overlay &overlay)
{
    ExportPlan plan;
    plan.outputSize = outputSizeFor(info.size, settings.height);
    plan.overlayRect = overlay.boundingRect(plan.outputSize);
    plan.fpsNum = info.fpsNum;
    plan.fpsDen = info.fpsDen;
    plan.startTime = std::clamp(settings.start, 0.0, std::max(0.0, info.duration));
    const double available = std::max(0.0, info.duration - plan.startTime);
    const double length = settings.duration > 0.0 ? std::min(settings.duration, available) : available;
    const bool piped = !plan.overlayRect.isEmpty();
    // The output is as long as the shorter of the video and the pictures.
    // The exact count is used when the container gives it; otherwise the
    // estimate is rounded down, because one picture too many would be written
    // into a pipe ffmpeg has already left.
    const bool wholeVideo = plan.startTime <= 0.0 && settings.duration <= 0.0;
    const qint64 estimated = std::max<qint64>(1, qint64(std::floor(length * info.fps() + 1e-6)));
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
            args << "-f" << "rawvideo" << "-pixel_format" << "rgba"
                 << "-video_size" << QString("%1x%2").arg(plan.overlayRect.width()).arg(plan.overlayRect.height())
                 << "-framerate" << QString("%1/%2").arg(plan.fpsNum).arg(plan.fpsDen)
                 << "-i" << "pipe:0";
        }

    QStringList chain;
    if (plan.outputSize != info.size)
        {
            chain << QString("scale=%1:%2").arg(plan.outputSize.width()).arg(plan.outputSize.height());
        }
    QString graph;
    if (piped)
        {
            // QPainter produces premultiplied alpha; the output ends when
            // the pictures do
            const QString overlayFilter = QString("overlay=%1:%2:alpha=premultiplied:eof_action=endall")
                                          .arg(plan.overlayRect.x()).arg(plan.overlayRect.y());
            const QString base = chain.isEmpty() ? QStringLiteral("[0:v]")
                                 : QString("[0:v]%1[base];[base]").arg(chain.join(','));
            graph = QString("%1[1:v]%2,%3[v]").arg(base, overlayFilter, VideoEncoders::outputFilter(encoder));
        }
    else
        {
            chain << VideoEncoders::outputFilter(encoder);
            graph = QString("[0:v]%1[v]").arg(chain.join(','));
        }
    args << "-filter_complex" << graph << "-map" << "[v]";

    switch (settings.audio)
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
