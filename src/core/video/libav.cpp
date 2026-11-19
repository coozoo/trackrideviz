#include "libav.h"

#include <QDir>
#include <QLoggingCategory>
#include <QFile>

#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef RTV_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/ffversion.h>
#include <libavutil/hwcontext.h>
}

// the oldest the program is written for: FFmpeg 6.1 (stream side data in the codec parameters)
static_assert(LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(60, 16, 100), "FFmpeg 6.1 or later is needed");

namespace
{
Q_LOGGING_CATEGORY(libavLog, "rtv.libav")

// The libraries' messages as the program's own (the Diagnostics panel, the command line with
// --verbose), not straight onto the terminal: trying encoders that are not there is loud.
void toQt(void *source, int level, const char *format, va_list arguments)
{
    if (level > av_log_get_level())
        {
            return;
        }
    char line[1024];
    int prefix = 1;
    av_log_format_line2(source, level, format, arguments, line, sizeof(line), &prefix);
    const QString text = QString::fromUtf8(line).trimmed();
    if (!text.isEmpty())
        {
            qCInfo(libavLog).noquote() << text;
        }
}

void quietLibraries()
{
    static const bool done = []()
    {
        av_log_set_level(AV_LOG_ERROR);
        av_log_set_callback(toQt);
        // libva writes its own lines about every device it opens; errors only
        if (!qEnvironmentVariableIsSet("LIBVA_MESSAGING_LEVEL"))
            {
                qputenv("LIBVA_MESSAGING_LEVEL", "1");
            }
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

void Libav::quiet()
{
#ifdef RTV_LIBAV
    quietLibraries();
#endif
}

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
    // the libraries take file names in UTF-8 everywhere (Windows included)
    const int opened = avformat_open_input(&format, videoPath.toUtf8().constData(), nullptr, nullptr);
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

bool Libav::hasEncoder(const QString &id)
{
#ifdef RTV_LIBAV
    return avcodec_find_encoder_by_name(id.toUtf8().constData()) != nullptr;
#else
    Q_UNUSED(id);
    return false;
#endif
}

bool Libav::encoderWorks(const QString &id, const QStringList &options, const QString &vaapiDevice)
{
#ifdef RTV_LIBAV
    quietLibraries();
    const AVCodec *codec = avcodec_find_encoder_by_name(id.toUtf8().constData());
    if (!codec)
        {
            return false;
        }
    const int width = 320;
    const int height = 240;
    AVCodecContext *encoder = avcodec_alloc_context3(codec);
    AVBufferRef *device = nullptr;
    AVBufferRef *frames = nullptr;
    AVFrame *picture = av_frame_alloc();
    AVFrame *uploaded = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVDictionary *settings = nullptr;
    bool works = false;
    encoder->width = width;
    encoder->height = height;
    encoder->time_base = AVRational{1, 30};
    encoder->framerate = AVRational{30, 1};
    // what the export's graph gives graphics cards (see VideoEncoders::outputFilter)
    encoder->pix_fmt = AV_PIX_FMT_NV12;
    for (int i = 0; i + 1 < options.size(); i += 2)
        {
            QString name = options.at(i).mid(1);
            name.truncate(name.indexOf(':') >= 0 ? name.indexOf(':') : name.size());
            av_dict_set(&settings, name.toUtf8().constData(), options.at(i + 1).toUtf8().constData(), 0);
        }
    picture->format = AV_PIX_FMT_NV12;
    picture->width = width;
    picture->height = height;
    bool ready = av_frame_get_buffer(picture, 0) >= 0;
    if (ready)
        {
            // black
            memset(picture->data[0], 16, size_t(picture->linesize[0]) * height);
            memset(picture->data[1], 128, size_t(picture->linesize[1]) * height / 2);
        }
    if (ready && !vaapiDevice.isEmpty())
        {
            ready = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, vaapiDevice.toUtf8().constData(), nullptr, 0) >= 0;
            if (ready)
                {
                    frames = av_hwframe_ctx_alloc(device);
                    auto *context = reinterpret_cast<AVHWFramesContext *>(frames->data);
                    context->format = AV_PIX_FMT_VAAPI;
                    context->sw_format = AV_PIX_FMT_NV12;
                    context->width = width;
                    context->height = height;
                    context->initial_pool_size = 4;
                    ready = av_hwframe_ctx_init(frames) >= 0;
                }
            if (ready)
                {
                    encoder->pix_fmt = AV_PIX_FMT_VAAPI;
                    encoder->hw_frames_ctx = av_buffer_ref(frames);
                    uploaded = av_frame_alloc();
                    ready = av_hwframe_get_buffer(frames, uploaded, 0) >= 0
                            && av_hwframe_transfer_data(uploaded, picture, 0) >= 0;
                }
        }
    if (ready && avcodec_open2(encoder, codec, &settings) >= 0)
        {
            // a few pictures and the end: some encoders give their first packet only then
            AVFrame *sent = uploaded ? uploaded : picture;
            bool fed = true;
            for (int i = 0; i < 3 && fed; ++i)
                {
                    sent->pts = i;
                    fed = avcodec_send_frame(encoder, sent) >= 0;
                }
            if (fed && avcodec_send_frame(encoder, nullptr) >= 0)
                {
                    while (avcodec_receive_packet(encoder, packet) >= 0)
                        {
                            works = true;
                            av_packet_unref(packet);
                        }
                }
        }
    av_dict_free(&settings);
    av_packet_free(&packet);
    av_frame_free(&uploaded);
    av_frame_free(&picture);
    avcodec_free_context(&encoder);
    av_buffer_unref(&frames);
    av_buffer_unref(&device);
    return works;
#else
    Q_UNUSED(id);
    Q_UNUSED(options);
    Q_UNUSED(vaapiDevice);
    return false;
#endif
}
