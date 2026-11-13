#include "videojoin.h"

#include "../sync/videosync.h"

#include <QCollator>
#include <QFileInfo>
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

QDateTime recordedAt(const VideoJoin::Part &part)
{
    const QDateTime fromName = VideoSync::wallClockFromFileName(QFileInfo(part.path).fileName());
    return fromName.isValid() ? fromName : part.info.created;
}
}

QStringList VideoJoin::ordered(const QStringList &paths)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    QStringList sorted = paths;
    std::sort(sorted.begin(), sorted.end(), [&collator](const QString &a, const QString &b)
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
        return collator.compare(nameA, nameB) < 0;
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
                    return tr("%1 is %2 %3×%4, the first video %5 %6×%7: only videos of the same kind can be joined.")
                           .arg(name, info.videoCodec).arg(info.size.width()).arg(info.size.height())
                           .arg(first.videoCodec).arg(first.size.width()).arg(first.size.height());
                }
            if (std::fabs(info.fps() - first.fps()) > 0.01)
                {
                    return tr("%1 has %2 frames per second, the first video %3: only videos of the same kind can be joined.")
                           .arg(name).arg(info.fps(), 0, 'f', 2).arg(first.fps(), 0, 'f', 2);
                }
            if (info.hasAudio != first.hasAudio || info.audioCodec != first.audioCodec)
                {
                    return tr("The sound of %1 differs from that of the first video: only videos of the same kind can be joined.")
                           .arg(name);
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

QStringList VideoJoin::arguments(const QString &listFile, const QString &output)
{
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", listFile,
                        // picture and sound; camera data streams do not fit every container
                        "-map", "0:v:0", "-map", "0:a?", "-c", "copy"
                       };
    const QString suffix = QFileInfo(output).suffix().toLower();
    if (suffix == "mp4" || suffix == "mov" || suffix == "m4v")
        {
            args << "-movflags" << "+faststart";
        }
    args << output;
    return args;
}
