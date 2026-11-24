#include "libav.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QFile>

#include <algorithm>
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

// changesSettings looks at the first picture after this many places along the file
const int kSettingsPlaces = 24;

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
                    result.audioTimeBase = QString("%1/%2").arg(stream->time_base.num).arg(stream->time_base.den);
                    result.audioSampleRate = codec->sample_rate;
                    result.audioChannels = codec->ch_layout.nb_channels;
                }
            // cover art is stored as a one-frame "video" stream
            const bool coverArt = stream->disposition & AV_DISPOSITION_ATTACHED_PIC;
            if (codec->codec_type != AVMEDIA_TYPE_VIDEO || haveVideo || coverArt)
                {
                    continue;
                }
            haveVideo = true;
            result.videoCodec = QString::fromLatin1(avcodec_get_name(codec->codec_id));
            result.videoTimeBase = QString("%1/%2").arg(stream->time_base.num).arg(stream->time_base.den);
            if (codec->extradata && codec->extradata_size > 0)
                {
                    // as ffprobe -show_data_hash sha256 tells it
                    result.videoSetup = QString::fromLatin1(QCryptographicHash::hash(
                                            QByteArray::fromRawData(reinterpret_cast<const char *>(codec->extradata), codec->extradata_size),
                                            QCryptographicHash::Sha256).toHex());
                }
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

double Libav::duration(const QString &path)
{
#ifdef RTV_LIBAV
    quietLibraries();
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0)
        {
            return 0.0;
        }
    double seconds = 0.0;
    if (avformat_find_stream_info(format, nullptr) >= 0 && format->duration != AV_NOPTS_VALUE)
        {
            seconds = double(format->duration) / AV_TIME_BASE;
        }
    avformat_close_input(&format);
    return std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
#else
    Q_UNUSED(path);
    return 0.0;
#endif
}

bool Libav::changesSettings(const QString &path)
{
#ifdef RTV_LIBAV
    quietLibraries();
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, path.toUtf8().constData(), nullptr, nullptr) < 0)
        {
            return false;
        }
    const auto closing = qScopeGuard([&format]() { avformat_close_input(&format); });
    if (avformat_find_stream_info(format, nullptr) < 0)
        {
            return false;
        }
    const int index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0)
        {
            return false;
        }
    const AVCodecParameters *codec = format->streams[index]->codecpar;
    const bool hevc = codec->codec_id == AV_CODEC_ID_HEVC;
    const QByteArray header(reinterpret_cast<const char *>(codec->extradata), codec->extradata_size);
    // the settings in the header, as MP4 and Matroska keep them (avcC, hvcC): each with its length
    // in front; a stream that has its settings only inside (Annex B) has nothing to differ from
    if ((codec->codec_id != AV_CODEC_ID_H264 && !hevc) || header.size() < (hevc ? 23 : 7) || header.at(0) != 1)
        {
            return false;
        }
    auto be16 = [](const QByteArray &bytes, int at) { return (uchar(bytes.at(at)) << 8) | uchar(bytes.at(at + 1)); };
    QList<QByteArray> known;
    int lengthSize = 4;
    if (hevc)
        {
            lengthSize = (uchar(header.at(21)) & 3) + 1;
            int at = 23;
            for (int array = 0; array < uchar(header.at(22)) && at + 3 <= header.size(); ++array)
                {
                    const int count = be16(header, at + 1);
                    at += 3;
                    for (int i = 0; i < count && at + 2 <= header.size(); ++i)
                        {
                            const int length = be16(header, at);
                            known << header.mid(at + 2, length);
                            at += 2 + length;
                        }
                }
        }
    else
        {
            lengthSize = (uchar(header.at(4)) & 3) + 1;
            int at = 6;
            for (int i = 0; i < (uchar(header.at(5)) & 0x1f) && at + 2 <= header.size(); ++i)
                {
                    const int length = be16(header, at);
                    known << header.mid(at + 2, length);
                    at += 2 + length;
                }
        }
    // a sequence parameter set in a picture's packet that the header does not hold
    const int setType = hevc ? 33 : 7;
    auto differs = [&](const AVPacket *packet)
    {
        const QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char *>(packet->data), packet->size);
        int at = 0;
        while (at + lengthSize < data.size())
            {
                qint64 length = 0;
                for (int i = 0; i < lengthSize; ++i)
                    {
                        length = (length << 8) | uchar(data.at(at + i));
                    }
                at += lengthSize;
                if (length <= 0 || at + length > data.size())
                    {
                        break;
                    }
                const int type = hevc ? (uchar(data.at(at)) >> 1) & 0x3f : uchar(data.at(at)) & 0x1f;
                if (type == setType && !known.contains(data.mid(at, int(length))))
                    {
                        return true;
                    }
                at += int(length);
            }
        return false;
    };
    // the first picture that starts afresh at some places along the file
    const AVStream *stream = format->streams[index];
    const qint64 length = format->duration > 0 ? format->duration : 0;
    AVPacket *packet = av_packet_alloc();
    const auto freeing = qScopeGuard([&packet]() { av_packet_free(&packet); });
    for (int place = 0; place < kSettingsPlaces; ++place)
        {
            const qint64 at = av_rescale_q(length * place / kSettingsPlaces, AVRational{1, AV_TIME_BASE}, stream->time_base);
            if (av_seek_frame(format, index, at, AVSEEK_FLAG_BACKWARD) < 0)
                {
                    continue;
                }
            for (int read = 0; read < 200 && av_read_frame(format, packet) >= 0; ++read)
                {
                    const bool start = packet->stream_index == index && (packet->flags & AV_PKT_FLAG_KEY);
                    const bool changed = start && differs(packet);
                    av_packet_unref(packet);
                    if (changed)
                        {
                            return true;
                        }
                    if (start)
                        {
                            break;
                        }
                }
        }
    return false;
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool Libav::copyJoin(const QStringList &paths, const QString &output, bool cameraData,
                     const std::function<bool(double seconds)> &progress, QString *error)
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
    AVFormatContext *input = nullptr;
    AVFormatContext *out = nullptr;
    AVPacket *packet = av_packet_alloc();
    struct Free
    {
        AVFormatContext **input;
        AVFormatContext **out;
        AVPacket **packet;
        ~Free()
        {
            avformat_close_input(input);
            if (*out && !((*out)->oformat->flags & AVFMT_NOFILE))
                {
                    avio_closep(&(*out)->pb);
                }
            avformat_free_context(*out);
            av_packet_free(packet);
        }
    } freeLater{&input, &out, &packet};
    auto unreadable = [&](const QString &path, int code)
    {
        return fail(QCoreApplication::translate("LibavExport", "%1 cannot be read: %2").arg(path, errorText(code)));
    };
    const QByteArray outPath = output.toUtf8();

    // Each file is read on its own, not through ffmpeg's concat, which takes the times of
    // every file in the clock of the first: a Matroska part (1/1000) and an MP4 (1/15360)
    // came out sixteen times too long. A file's times are moved on by the length of those
    // before it, as concat does.
    int64_t offsetUs = 0;       // where the file being read starts in the output
    std::vector<int64_t> lastDts;
    int sinceTold = 0;
    for (int f = 0; f < paths.size(); ++f)
        {
            const QString &path = paths.at(f);
            avformat_close_input(&input);
            int result = avformat_open_input(&input, path.toUtf8().constData(), nullptr, nullptr);
            if (result < 0 || (result = avformat_find_stream_info(input, nullptr)) < 0)
                {
                    return unreadable(path, result);
                }
            // -map 0:v:0 -map 0:a? [-map 0:d? -tag:d gpmd]: each kind's streams in order, by
            // the place they have in the first file; where each input stream goes, -1 nowhere
            std::vector<int> into(input->nb_streams, -1);
            int videos = 0;
            int audios = 0;
            int datas = 0;
            for (unsigned i = 0; i < input->nb_streams; ++i)
                {
                    const AVStream *stream = input->streams[i];
                    const AVMediaType type = stream->codecpar->codec_type;
                    const bool coverArt = stream->disposition & AV_DISPOSITION_ATTACHED_PIC;
                    int place = -1;
                    if (type == AVMEDIA_TYPE_VIDEO && !coverArt && videos == 0)
                        {
                            place = videos++;
                        }
                    else if (type == AVMEDIA_TYPE_AUDIO)
                        {
                            place = audios++;
                        }
                    else if (type == AVMEDIA_TYPE_DATA && cameraData && stream->codecpar->codec_tag == MKTAG('g', 'p', 'm', 'd'))
                        {
                            // the GPMF track alone: a GoPro file's timecode (tmcd) and its own
                            // (fdsc) are data too, and go nowhere, as through ffmpeg's concat
                            place = datas++;
                        }
                    if (place < 0)
                        {
                            continue;
                        }
                    if (f == 0)
                        {
                            if (!out)
                                {
                                    result = avformat_alloc_output_context2(&out, nullptr, nullptr, outPath.constData());
                                    if (result < 0 || !out)
                                        {
                                            return fail(QString("The output file's type: %1").arg(errorText(result)));
                                        }
                                }
                            AVStream *copy = avformat_new_stream(out, nullptr);
                            avcodec_parameters_copy(copy->codecpar, stream->codecpar);
                            copy->time_base = stream->time_base;
                            copy->disposition = stream->disposition;
                            av_dict_copy(&copy->metadata, stream->metadata, 0);
                            if (type == AVMEDIA_TYPE_DATA)
                                {
                                    copy->codecpar->codec_tag = MKTAG('g', 'p', 'm', 'd');
                                }
                            else
                                {
                                    // the file's own tag (hvc1 rather than hev1) where the container takes it,
                                    // as ffmpeg keeps it
                                    unsigned tag = 0;
                                    const AVCodecTag *const *tags = out->oformat->codec_tag;
                                    const bool keeps = !tags
                                                       || av_codec_get_id(tags, stream->codecpar->codec_tag) == stream->codecpar->codec_id
                                                       || !av_codec_get_tag2(tags, stream->codecpar->codec_id, &tag);
                                    copy->codecpar->codec_tag = keeps ? stream->codecpar->codec_tag : 0;
                                }
                            into[i] = copy->index;
                            continue;
                        }
                    // the output stream of the same kind and place, if the first file had one
                    int seen = 0;
                    for (unsigned o = 0; o < out->nb_streams; ++o)
                        {
                            if (out->streams[o]->codecpar->codec_type == type && seen++ == place)
                                {
                                    into[i] = int(o);
                                    break;
                                }
                        }
                }
            if (f == 0)
                {
                    if (videos == 0)
                        {
                            return fail(QCoreApplication::translate("LibavExport", "%1 has no picture").arg(path));
                        }
                    // the recording's date and the like, as ffmpeg takes them from its first input
                    av_dict_copy(&out->metadata, input->metadata, 0);
                    if (!(out->oformat->flags & AVFMT_NOFILE)
                            && (result = avio_open(&out->pb, outPath.constData(), AVIO_FLAG_WRITE)) < 0)
                        {
                            return fail(QCoreApplication::translate("LibavExport", "%1 cannot be written: %2")
                                        .arg(output, errorText(result)));
                        }
                    AVDictionary *muxerOptions = nullptr;
                    const QString muxer = QString::fromLatin1(out->oformat->name);
                    if (muxer.contains("mp4") || muxer.contains("mov"))
                        {
                            av_dict_set(&muxerOptions, "movflags", "+faststart", 0);
                        }
                    result = avformat_write_header(out, &muxerOptions);
                    av_dict_free(&muxerOptions);
                    if (result < 0)
                        {
                            return fail(QString("Starting the output file: %1").arg(errorText(result)));
                        }
                    lastDts.assign(out->nb_streams, AV_NOPTS_VALUE);
                }

            const int64_t fileStart = input->start_time == AV_NOPTS_VALUE ? 0 : input->start_time;
            int64_t endUs = offsetUs;
            while ((result = av_read_frame(input, packet)) >= 0)
                {
                    const int to = packet->stream_index < int(into.size()) ? into[size_t(packet->stream_index)] : -1;
                    if (to < 0)
                        {
                            av_packet_unref(packet);
                            continue;
                        }
                    const AVRational inBase = input->streams[packet->stream_index]->time_base;
                    AVStream *stream = out->streams[to];
                    // the file's own time 0 at its place in the output
                    const int64_t shift = av_rescale_q(offsetUs - fileStart, AV_TIME_BASE_Q, inBase);
                    if (packet->pts != AV_NOPTS_VALUE)
                        {
                            packet->pts += shift;
                        }
                    if (packet->dts != AV_NOPTS_VALUE)
                        {
                            packet->dts += shift;
                        }
                    const int64_t at = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
                    if (at != AV_NOPTS_VALUE)
                        {
                            endUs = std::max(endUs, av_rescale_q(at + std::max<int64_t>(packet->duration, 0), inBase,
                                                                 AV_TIME_BASE_Q));
                        }
                    av_packet_rescale_ts(packet, inBase, stream->time_base);
                    packet->stream_index = to;
                    packet->pos = -1;
                    // where two files meet the times may step back a little: on, as ffmpeg puts them.
                    // A Matroska file's first picture has no decoding time: past the first file,
                    // the one after the last (the muxer would guess one from before it)
                    int64_t &last = lastDts[size_t(to)];
                    if (packet->dts == AV_NOPTS_VALUE && last != AV_NOPTS_VALUE)
                        {
                            packet->dts = last + 1;
                            if (packet->pts != AV_NOPTS_VALUE)
                                {
                                    packet->pts = std::max(packet->pts, packet->dts);
                                }
                        }
                    else if (packet->dts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && packet->dts <= last)
                        {
                            const int64_t moved = last + 1;
                            if (packet->pts != AV_NOPTS_VALUE && packet->pts >= packet->dts)
                                {
                                    packet->pts = std::max(packet->pts, moved);
                                }
                            packet->dts = moved;
                        }
                    if (packet->dts != AV_NOPTS_VALUE)
                        {
                            last = packet->dts;
                        }
                    const bool isPicture = stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
                    const double seconds = packet->dts != AV_NOPTS_VALUE ? packet->dts * av_q2d(stream->time_base) : -1.0;
                    result = av_interleaved_write_frame(out, packet);
                    if (result < 0)
                        {
                            return fail(QString("Writing the file: %1").arg(errorText(result)));
                        }
                    if (isPicture && seconds >= 0.0 && ++sinceTold >= 30)
                        {
                            sinceTold = 0;
                            if (progress && !progress(seconds))
                                {
                                    return fail(QString());
                                }
                        }
                }
            if (result != AVERROR_EOF)
                {
                    return unreadable(path, result);
                }
            // the next file follows this one's length (what the container says, as concat takes it)
            offsetUs = input->duration != AV_NOPTS_VALUE && input->duration > 0 ? offsetUs + input->duration : endUs;
        }
    if (!out)
        {
            return fail(QCoreApplication::translate("VideoJoin", "Choose two or more videos."));
        }
    const int result = av_write_trailer(out);
    if (result < 0)
        {
            return fail(QString("Finishing the file: %1").arg(errorText(result)));
        }
    return true;
#else
    Q_UNUSED(paths);
    Q_UNUSED(output);
    Q_UNUSED(cameraData);
    Q_UNUSED(progress);
    return fail(QCoreApplication::translate("LibavExport", "Built without FFmpeg's libraries"));
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
    // why a test fails, for the Diagnostics panel: "graphics card's encoder missing" is
    // otherwise all anyone sees
    QString failed;
    if (!ready)
        {
            failed = vaapiDevice.isEmpty() ? QStringLiteral("no picture for it")
                     : QString("the graphics card %1 or its picture memory").arg(vaapiDevice);
        }
    const int opened = ready ? avcodec_open2(encoder, codec, &settings) : 0;
    if (ready && opened < 0)
        {
            failed = "opening it: " + errorText(opened);
        }
    if (ready && opened >= 0)
        {
            // a few pictures and the end: some encoders give their first packet only then. What
            // is ready is taken after each: one that holds a few answers EAGAIN until it is
            AVFrame *sent = uploaded ? uploaded : picture;
            int result = 0;
            auto takePackets = [&]()
            {
                int received = 0;
                while ((received = avcodec_receive_packet(encoder, packet)) >= 0)
                    {
                        works = true;
                        av_packet_unref(packet);
                    }
                return received;
            };
            int received = 0;
            for (int i = 0; i <= 3 && result >= 0; ++i)
                {
                    if (i < 3)
                        {
                            sent->pts = i;
                        }
                    // the fourth: the end
                    result = avcodec_send_frame(encoder, i < 3 ? sent : nullptr);
                    if (result == AVERROR(EAGAIN))
                        {
                            // full: emptied, then the same again
                            received = takePackets();
                            result = avcodec_send_frame(encoder, i < 3 ? sent : nullptr);
                        }
                    if (result >= 0)
                        {
                            received = takePackets();
                        }
                }
            if (result < 0)
                {
                    // a packet or two before it refused one is not working
                    works = false;
                    failed = "sending it pictures: " + errorText(result);
                }
            else if (!works)
                {
                    failed = "no packet back: " + errorText(received);
                }
        }
    if (!works)
        {
            qCInfo(libavLog).noquote() << QString("test encode with %1 failed, %2").arg(id, failed);
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
