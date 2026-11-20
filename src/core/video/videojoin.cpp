#include "videojoin.h"

#include "../sync/videosync.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace
{
// Camera chapter names: GX010123.MP4 is chapter 1 of recording 0123, GX020123 chapter 2;
// the older GOPR0123 is the first chapter and GP010123 the second.
bool chapterKey(const QString &fileName, QString *recording, int *chapter)
{
    static const QRegularExpression modern("^G[HXLP](\\d{2})(\\d{4})\\.", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression old("^GOPR(\\d{4})\\.", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = old.match(fileName);
    if (match.hasMatch())
        {
            *recording = match.captured(1);
            *chapter = 0;
            return true;
        }
    match = modern.match(fileName);
    if (match.hasMatch())
        {
            *recording = match.captured(2);
            *chapter = match.captured(1).toInt();
            return true;
        }
    return false;
}

// The same for a name that only starts with a chapter's, such as that of a joined video.
// `recording` holds the letter of the camera's naming too: GH and GX recordings are not one.
bool chapterPrefix(const QString &fileName, QString *recording, int *chapter)
{
    static const QRegularExpression modern("^G([HXP])(\\d{2})(\\d{4})(?=[._])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression old("^GOPR(\\d{4})(?=[._])", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = old.match(fileName);
    if (match.hasMatch())
        {
            // the older cameras' first chapter; the others are GP01, GP02... (chapters 1, 2...)
            *recording = "P" + match.captured(1);
            *chapter = 0;
            return true;
        }
    match = modern.match(fileName);
    if (match.hasMatch())
        {
            *recording = match.captured(1).toUpper() + match.captured(3);
            *chapter = match.captured(2).toInt();
            return true;
        }
    return false;
}

QDateTime recordedAt(const VideoJoin::Part &part)
{
    const QDateTime fromName = VideoSync::wallClockFromFileName(QFileInfo(part.path).fileName());
    return fromName.isValid() ? fromName : part.info.created;
}

// clip9 before clip10, whatever the case of the letters. Done by hand:
// QCollator's numeric mode does nothing in the "C" locale.
bool naturalLess(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size())
        {
            if (a.at(i).isDigit() && b.at(j).isDigit())
                {
                    int endA = i;
                    int endB = j;
                    while (endA < a.size() && a.at(endA).isDigit())
                        {
                            ++endA;
                        }
                    while (endB < b.size() && b.at(endB).isDigit())
                        {
                            ++endB;
                        }
                    // without leading zeros the longer number is the larger one
                    while (i < endA - 1 && a.at(i) == QLatin1Char('0'))
                        {
                            ++i;
                        }
                    while (j < endB - 1 && b.at(j) == QLatin1Char('0'))
                        {
                            ++j;
                        }
                    if (endA - i != endB - j)
                        {
                            return endA - i < endB - j;
                        }
                    const int order = QStringView(a).mid(i, endA - i).compare(QStringView(b).mid(j, endB - j));
                    if (order != 0)
                        {
                            return order < 0;
                        }
                    i = endA;
                    j = endB;
                    continue;
                }
            const QChar ca = a.at(i).toLower();
            const QChar cb = b.at(j).toLower();
            if (ca != cb)
                {
                    return ca < cb;
                }
            ++i;
            ++j;
        }
    return a.size() - i < b.size() - j;
}
}

QStringList VideoJoin::ordered(const QStringList &paths)
{
    QStringList sorted = paths;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b)
    {
        const QString nameA = QFileInfo(a).fileName();
        const QString nameB = QFileInfo(b).fileName();
        QString recordingA;
        QString recordingB;
        int chapterA = 0;
        int chapterB = 0;
        if (chapterKey(nameA, &recordingA, &chapterA) && chapterKey(nameB, &recordingB, &chapterB))
            {
                return recordingA != recordingB ? recordingA < recordingB : chapterA < chapterB;
            }
        return naturalLess(nameA, nameB);
    });
    return sorted;
}

QString VideoJoin::whyNot(const QVector<Part> &parts)
{
    if (parts.size() < 2)
        {
            return tr("Choose two or more videos.");
        }
    const MediaInfo &first = parts.first().info;
    for (const Part &part : parts)
        {
            const MediaInfo &info = part.info;
            const QString name = QFileInfo(part.path).fileName();
            if (info.videoCodec != first.videoCodec || info.size != first.size)
                {
                    return tr("%1 is %2 %3×%4, the first video %5 %6×%7.")
                           .arg(name, info.videoCodec, QString::number(info.size.width()), QString::number(info.size.height()),
                                first.videoCodec, QString::number(first.size.width()), QString::number(first.size.height()));
                }
            if (std::fabs(info.fps() - first.fps()) > 0.01)
                {
                    return tr("%1 has %2 frames per second, the first video %3.")
                           .arg(name, QString::number(info.fps(), 'f', 2), QString::number(first.fps(), 'f', 2));
                }
            if (!info.videoSetup.isEmpty() && !first.videoSetup.isEmpty() && info.videoSetup != first.videoSetup)
                {
                    return tr("%1 was made by another encoder, or with other settings, than the first video: "
                              "its pictures would not decode after a copy.").arg(name);
                }
            if (info.hasAudio != first.hasAudio || info.audioCodec != first.audioCodec
                    || info.audioSampleRate != first.audioSampleRate || info.audioChannels != first.audioChannels)
                {
                    return tr("The sound of %1 differs from that of the first video.")
                           .arg(name);
                }
        }
    return QString();
}

QString VideoJoin::whyNotByProgram(const QVector<Part> &parts)
{
    if (parts.isEmpty())
        {
            return QString();
        }
    const MediaInfo &first = parts.first().info;
    for (const Part &part : parts)
        {
            if (part.info.videoTimeBase != first.videoTimeBase || part.info.audioTimeBase != first.audioTimeBase)
                {
                    return tr("%1 keeps its times otherwise than the first video (another kind of file): the ffmpeg "
                              "program cannot copy them together.").arg(QFileInfo(part.path).fileName());
                }
        }
    return QString();
}

QVector<TimeShift> VideoJoin::shifts(const QVector<Part> &parts)
{
    QVector<TimeShift> shifts;
    if (parts.isEmpty())
        {
            return shifts;
        }
    const QDateTime first = recordedAt(parts.first());
    double start = parts.first().info.duration;
    double shift = 0.0;
    for (int i = 1; i < parts.size(); ++i)
        {
            const QDateTime recorded = recordedAt(parts.at(i));
            if (first.isValid() && recorded.isValid())
                {
                    // how much later it began than it would following straight on
                    const double gap = first.msecsTo(recorded) / 1000.0 - start;
                    // a second or so is the clock's resolution: chapters follow on
                    const double next = gap > 1.0 ? gap : 0.0;
                    if (next != shift)
                        {
                            shift = next;
                            shifts.append({start, shift});
                        }
                }
            start += parts.at(i).info.duration;
        }
    return shifts;
}

QByteArray VideoJoin::listFile(const QStringList &paths)
{
    QByteArray list = "ffconcat version 1.0\n";
    for (const QString &path : paths)
        {
            // quoted; a quote itself is closed, escaped and opened again
            QString quoted = path;
            quoted.replace('\'', "'\\''");
            list += "file '" + quoted.toUtf8() + "'\n";
        }
    return list;
}

QStringList VideoJoin::chaptersFrom(const QString &path)
{
    const QFileInfo info(path);
    QString recording;
    int first = 0;
    if (!chapterPrefix(info.fileName(), &recording, &first))
        {
            return {};
        }
    QMap<int, QString> chapters;
    for (const QFileInfo &file : info.absoluteDir().entryInfoList(QDir::Files))
        {
            QString other;
            int chapter = 0;
            // the chapters themselves: their name is no more than the chapter's
            if (file.suffix().compare("mp4", Qt::CaseInsensitive) == 0 && file.completeBaseName().size() == 8
                    && chapterPrefix(file.fileName(), &other, &chapter) && other == recording && chapter >= first)
                {
                    chapters.insert(chapter, file.absoluteFilePath());
                }
        }
    // one after another: a missing chapter ends the recording here
    QStringList ordered;
    for (int chapter = first; chapters.contains(chapter); ++chapter)
        {
            ordered << chapters.value(chapter);
        }
    return ordered;
}

bool VideoJoin::keepsCameraData(const QVector<Part> &parts, const QString &output)
{
    const QString suffix = QFileInfo(output).suffix().toLower();
    return !parts.isEmpty() && (suffix == "mp4" || suffix == "mov" || suffix == "m4v")
           && std::all_of(parts.cbegin(), parts.cend(), [](const Part &part) { return part.info.hasCameraData; });
}

QStringList VideoJoin::arguments(const QString &listFile, const QString &output, bool cameraData)
{
    QStringList args = {"-hide_banner", "-loglevel", "error", "-nostats", "-progress", "pipe:1", "-y",
                        "-f", "concat", "-safe", "0", "-i", listFile,
                        "-map", "0:v:0", "-map", "0:a?"
                       };
    if (cameraData)
        {
            // read through concat only the GPMF track is a data stream (the timecode and the
            // camera's own are of no known kind); it needs its tag again to be one in the output
            args << "-map" << "0:d?" << "-tag:d" << "gpmd";
        }
    args << "-c" << "copy";
    const QString suffix = QFileInfo(output).suffix().toLower();
    if (suffix == "mp4" || suffix == "mov" || suffix == "m4v")
        {
            args << "-movflags" << "+faststart";
        }
    args << output;
    return args;
}

ExportPlan VideoJoin::encodePlan(const QVector<Part> &parts, const QString &output, const VideoEncoder &encoder,
                                 const ExportSettings &settings)
{
    ExportPlan plan;
    plan.globalOptions = VideoEncoders::globalArguments(encoder);
    for (const Part &part : parts)
        {
            plan.inputs.append({QStringList(), part.path});
        }
    const MediaInfo &first = parts.first().info;
    // even sizes: every encoder takes those
    const int width = first.size.width() / 2 * 2;
    const int height = first.size.height() / 2 * 2;
    const QString fps = QString("%1/%2").arg(first.fpsNum).arg(first.fpsDen);
    const bool sound = std::any_of(parts.cbegin(), parts.cend(), [](const Part &part) { return part.info.hasAudio; });
    QStringList chains;
    QString inputs;
    for (int i = 0; i < parts.size(); ++i)
        {
            // the sound exactly as long as the picture: the concat filter waits for the longer
            // one, and AAC's few extra milliseconds would leave a frame missing at each seam
            const MediaInfo &info = parts.at(i).info;
            const double picture = info.frameCount > 0 && info.fps() > 0.0 ? info.frameCount / info.fps() : info.duration;
            chains << QString("[%1:v:0]scale=%2:%3:force_original_aspect_ratio=decrease,"
                              "pad=%2:%3:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=%4,format=yuv420p[v%1]")
                      .arg(i).arg(width).arg(height).arg(fps);
            inputs += QString("[v%1]").arg(i);
            if (!sound)
                {
                    continue;
                }
            if (parts.at(i).info.hasAudio)
                {
                    chains << QString("[%1:a:0]aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,"
                                      "apad,atrim=duration=%2[a%1]").arg(i).arg(picture, 0, 'f', 6);
                }
            else
                {
                    chains << QString("anullsrc=r=48000:cl=stereo,atrim=duration=%2[a%1]").arg(i).arg(picture, 0, 'f', 6);
                }
            inputs += QString("[a%1]").arg(i);
        }
    chains << QString("%1concat=n=%2:v=1:a=%3[joined]%4").arg(inputs).arg(parts.size()).arg(sound ? 1 : 0)
              .arg(sound ? QStringLiteral("[a]") : QString())
           << QString("[joined]%1[v]").arg(VideoEncoders::outputFilter(encoder));
    plan.filterGraph = chains.join(';');
    if (sound)
        {
            plan.audioMap = "[a]";
            plan.audioOptions = {"-c:a", "aac", "-b:a", QString("%1k").arg(settings.audioKbps)};
        }
    plan.hasAudio = sound;
    plan.videoOptions = VideoEncoders::arguments(encoder, settings);
    if (ExportSettings::isMp4Family(QFileInfo(output).suffix()))
        {
            plan.muxerArguments = {"-movflags", "+faststart"};
        }
    plan.outputPath = output;
    plan.outputSize = QSize(width, height);
    plan.fpsNum = first.fpsNum;
    plan.fpsDen = first.fpsDen;
    plan.arguments = plan.ffmpegArguments();
    return plan;
}

QStringList VideoJoin::encodeArguments(const QVector<Part> &parts, const QString &output, const VideoEncoder &encoder,
                                       const ExportSettings &settings)
{
    QStringList args = encodePlan(parts, output, encoder, settings).ffmpegArguments();
    // after "-hide_banner -loglevel error"
    args.insert(3, "-nostats");
    args.insert(4, "-progress");
    args.insert(5, "pipe:1");
    return args;
}

double VideoJoin::progressSeconds(const QByteArray &progressOutput)
{
    // "out_time_us=12345678" lines, the last one counts
    double seconds = -1.0;
    for (const QByteArray &line : progressOutput.split('\n'))
        {
            if (line.startsWith("out_time_us="))
                {
                    bool ok = false;
                    const qint64 us = line.mid(12).trimmed().toLongLong(&ok);
                    if (ok && us >= 0)
                        {
                            seconds = us / 1e6;
                        }
                }
        }
    return seconds;
}
