#include "mediainfo.h"

#include "ffmpegtools.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdlib>

namespace
{
// "60000/1001" -> 60000, 1001. False for "0/0", which ffprobe writes for "unknown".
bool parseRate(const QString &text, int *num, int *den)
{
    const QStringList parts = text.split('/');
    bool okNum = false;
    bool okDen = true;
    const int n = parts.value(0).toInt(&okNum);
    const int d = parts.size() > 1 ? parts.at(1).toInt(&okDen) : 1;
    if (!okNum || !okDen || n <= 0 || d <= 0)
        {
            return false;
        }
    *num = n;
    *den = d;
    return true;
}

int rotationOf(const QJsonObject &stream)
{
    // newer files: display matrix side data; older: a "rotate" tag
    const QJsonArray sideData = stream.value("side_data_list").toArray();
    for (const QJsonValue &value : sideData)
        {
            const QJsonObject entry = value.toObject();
            if (entry.contains("rotation"))
                {
                    return int(std::lround(entry.value("rotation").toVariant().toDouble()));
                }
        }
    return stream.value("tags").toObject().value("rotate").toVariant().toInt();
}
}

bool MediaProbe::parse(const QByteArray &ffprobeJson, MediaInfo *info, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return false;
    };

    const QJsonObject root = QJsonDocument::fromJson(ffprobeJson).object();
    const QJsonArray streams = root.value("streams").toArray();
    MediaInfo result;
    bool haveVideo = false;
    for (const QJsonValue &value : streams)
        {
            const QJsonObject stream = value.toObject();
            const QString type = stream.value("codec_type").toString();
            if (type == "audio" && !result.hasAudio)
                {
                    result.hasAudio = true;
                    result.audioCodec = stream.value("codec_name").toString();
                }
            // cover art is stored as a one-frame "video" stream
            const bool coverArt = stream.value("disposition").toObject().value("attached_pic").toInt() != 0;
            if (type != "video" || haveVideo || coverArt)
                {
                    continue;
                }
            haveVideo = true;
            result.videoCodec = stream.value("codec_name").toString();
            result.size = QSize(stream.value("width").toInt(), stream.value("height").toInt());
            if (std::abs(rotationOf(stream)) % 180 == 90)
                {
                    result.size.transpose();
                }
            // the average rate is the right one for variable frame rate files;
            // r_frame_rate is the fallback when the container does not give it
            if (!parseRate(stream.value("avg_frame_rate").toString(), &result.fpsNum, &result.fpsDen))
                {
                    parseRate(stream.value("r_frame_rate").toString(), &result.fpsNum, &result.fpsDen);
                }
            result.duration = stream.value("duration").toVariant().toDouble();
            result.frameCount = stream.value("nb_frames").toVariant().toLongLong();
            result.bitrateKbps = int(stream.value("bit_rate").toVariant().toLongLong() / 1000);
        }
    if (!haveVideo)
        {
            return fail(tr("The file has no video."));
        }
    if (result.duration <= 0.0)
        {
            // Matroska keeps the duration only at file level
            result.duration = root.value("format").toObject().value("duration").toVariant().toDouble();
        }
    result.created = QDateTime::fromString(root.value("format").toObject().value("tags").toObject()
                                           .value("creation_time").toString(), Qt::ISODateWithMs);
    if (result.bitrateKbps <= 0)
        {
            result.bitrateKbps = int(root.value("format").toObject().value("bit_rate").toVariant().toLongLong() / 1000);
        }
    if (!result.isValid())
        {
            return fail(tr("The size or the frame rate of the video could not be read."));
        }
    *info = result;
    return true;
}

bool MediaProbe::probe(const QString &ffprobeProgram, const QString &videoPath, MediaInfo *info, QString *error)
{
    const FfmpegTools::RunResult run = FfmpegTools::run(ffprobeProgram,
    {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", videoPath});
    if (!run.started)
        {
            if (error)
                {
                    *error = tr("ffprobe could not be started (%1). It is installed together with ffmpeg; "
                                "its location can be set in the settings.").arg(ffprobeProgram);
                }
            return false;
        }
    if (run.exitCode != 0)
        {
            if (error)
                {
                    *error = tr("ffprobe cannot read the video: %1").arg(QString::fromLocal8Bit(run.errorOutput).trimmed());
                }
            return false;
        }
    return parse(run.output, info, error);
}
