#include "libav.h"

#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdlib>

#ifdef RTV_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/ffversion.h>
}

// the oldest the program is written for: FFmpeg 6.1 (stream side data in the codec parameters)
static_assert(LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(60, 16, 100), "FFmpeg 6.1 or later is needed");

namespace
{
// Only what goes wrong, and that through Qt's messages rather than the console alone.
void quietLibraries()
{
    static const bool done = []()
    {
        av_log_set_level(AV_LOG_ERROR);
        return true;
    }();
    Q_UNUSED(done);
}

QString errorText(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

bool fraction(AVRational rate, int *num, int *den)
{
    if (rate.num <= 0 || rate.den <= 0)
        {
            return false;
        }
    *num = rate.num;
    *den = rate.den;
    return true;
}

QString metadata(const AVDictionary *dictionary, const char *key)
{
    const AVDictionaryEntry *entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value) : QString();
}
}
#endif

bool Libav::available()
{
#ifdef RTV_LIBAV
    return true;
#else
    return false;
#endif
}

QString Libav::version()
{
#ifdef RTV_LIBAV
    auto part = [](unsigned version)
    {
        return QString("%1.%2").arg(AV_VERSION_MAJOR(version)).arg(AV_VERSION_MINOR(version));
    };
    return QString("FFmpeg %1 (libavformat %2, libavcodec %3, libavfilter %4)")
           .arg(QString::fromLatin1(av_version_info()), part(avformat_version()), part(avcodec_version()),
                part(avfilter_version()));
#else
    return QString();
#endif
}

bool Libav::probe(const QString &videoPath, MediaInfo *info, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return false;
    };
#ifdef RTV_LIBAV
    quietLibraries();
    AVFormatContext *format = nullptr;
    const int opened = avformat_open_input(&format, QFile::encodeName(videoPath).constData(), nullptr, nullptr);
    if (opened < 0)
        {
            return fail(MediaProbe::tr("The video cannot be read (%1): %2")
                        .arg(QDir::toNativeSeparators(videoPath), errorText(opened)));
        }
    // closed on every way out
    struct Closer
    {
        AVFormatContext **format;
        ~Closer() { avformat_close_input(format); }
    } closer{&format};
    const int found = avformat_find_stream_info(format, nullptr);
    if (found < 0)
        {
            return fail(MediaProbe::tr("The video cannot be read (%1): %2")
                        .arg(QDir::toNativeSeparators(videoPath), errorText(found)));
        }

    MediaInfo result;
    bool haveVideo = false;
    for (unsigned i = 0; i < format->nb_streams; ++i)
        {
            const AVStream *stream = format->streams[i];
            const AVCodecParameters *codec = stream->codecpar;
            if (codec->codec_tag == MKTAG('g', 'p', 'm', 'd'))
                {
                    result.hasCameraData = true;
                }
            if (codec->codec_type == AVMEDIA_TYPE_AUDIO && !result.hasAudio)
                {
                    result.hasAudio = true;
                    result.audioCodec = QString::fromLatin1(avcodec_get_name(codec->codec_id));
                }
            // cover art is stored as a one-frame "video" stream
            const bool coverArt = stream->disposition & AV_DISPOSITION_ATTACHED_PIC;
            if (codec->codec_type != AVMEDIA_TYPE_VIDEO || haveVideo || coverArt)
                {
                    continue;
                }
            haveVideo = true;
            result.videoCodec = QString::fromLatin1(avcodec_get_name(codec->codec_id));
            result.size = QSize(codec->width, codec->height);
            const AVPacketSideData *matrix = av_packet_side_data_get(codec->coded_side_data, codec->nb_coded_side_data,
                                             AV_PKT_DATA_DISPLAYMATRIX);
            if (matrix && matrix->size >= 9 * sizeof(int32_t))
                {
                    const double rotation = av_display_rotation_get(reinterpret_cast<const int32_t *>(matrix->data));
                    if (!std::isnan(rotation) && std::abs(int(std::lround(rotation))) % 180 == 90)
                        {
                            result.size.transpose();
                        }
                }
            // the average rate is the right one for variable frame rate files
            if (!fraction(stream->avg_frame_rate, &result.fpsNum, &result.fpsDen))
                {
                    fraction(stream->r_frame_rate, &result.fpsNum, &result.fpsDen);
                }
            if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
                {
                    result.duration = stream->duration * av_q2d(stream->time_base);
                }
            result.frameCount = stream->nb_frames;
            result.bitrateKbps = int(codec->bit_rate / 1000);
        }
    if (!haveVideo)
        {
            return fail(MediaProbe::tr("The file has no video."));
        }
    if (result.duration <= 0.0 && format->duration != AV_NOPTS_VALUE && format->duration > 0)
        {
            // Matroska keeps the duration only at file level
            result.duration = double(format->duration) / AV_TIME_BASE;
        }
    result.created = QDateTime::fromString(metadata(format->metadata, "creation_time"), Qt::ISODateWithMs);
    if (result.bitrateKbps <= 0)
        {
            result.bitrateKbps = int(format->bit_rate / 1000);
        }
    if (!result.isValid())
        {
            return fail(MediaProbe::tr("The size or the frame rate of the video could not be read."));
        }
    *info = result;
    return true;
#else
    Q_UNUSED(videoPath);
    Q_UNUSED(info);
    return fail(QString());
#endif
}
