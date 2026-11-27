#include "libavexport.h"

#include "libav.h"

#include <QCoreApplication>
#include <QImage>

#include <cstring>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
#include <vector>

#ifdef RTV_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace
{
QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

// The filters that turn a picture upright as its display matrix says (a phone held upright
// stores it on its side), as the ffmpeg program does on its own: the plan is laid out for
// the picture as shown. None for one shown as stored.
std::vector<std::pair<const char *, QString>> uprightFilters(const AVStream *stream)
{
    const AVPacketSideData *side = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                   stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!side || side->size < 9 * sizeof(int32_t))
        {
            return {};
        }
    const int32_t *matrix = reinterpret_cast<const int32_t *>(side->data);
    const double rotation = av_display_rotation_get(matrix);
    if (std::isnan(rotation))
        {
            return {};
        }
    // clockwise, 0 to 360
    double theta = -std::round(rotation);
    theta -= 360.0 * std::floor(theta / 360.0 + 0.9 / 360.0);
    std::vector<std::pair<const char *, QString>> filters;
    if (std::abs(theta - 90.0) < 1.0)
        {
            filters.push_back({"transpose", matrix[3] > 0 ? "cclock_flip" : "clock"});
        }
    else if (std::abs(theta - 180.0) < 1.0)
        {
            if (matrix[0] < 0)
                {
                    filters.push_back({"hflip", QString()});
                }
            if (matrix[4] < 0)
                {
                    filters.push_back({"vflip", QString()});
                }
        }
    else if (std::abs(theta - 270.0) < 1.0)
        {
            filters.push_back({"transpose", matrix[3] < 0 ? "clock_flip" : "cclock"});
        }
    else if (std::abs(theta) > 1.0)
        {
            filters.push_back({"rotate", QString("%1*PI/180").arg(theta, 0, 'f', 6)});
        }
    else if (matrix[4] < 0)
        {
            filters.push_back({"vflip", QString()});
        }
    return filters;
}

// ffmpeg's flag as libav names the option: "-b:v" -> "b", "-movflags" -> "movflags"
QByteArray optionName(const QString &flag)
{
    QString name = flag.mid(1);
    const int stream = name.indexOf(':');
    if (stream >= 0)
        {
            name.truncate(stream);
        }
    return name.toUtf8();
}

// The value after `flag` among ffmpeg's options; empty when it is not there.
QString valueOf(const QStringList &options, const char *flag)
{
    const int at = int(options.indexOf(QLatin1String(flag)));
    return at >= 0 && at + 1 < options.size() ? options.at(at + 1) : QString();
}

// The options of one part as a dictionary for libav, leaving out those handled by hand.
AVDictionary *dictionaryOf(const QStringList &options, const QStringList &leaveOut)
{
    AVDictionary *dictionary = nullptr;
    for (int i = 0; i + 1 < options.size(); i += 2)
        {
            if (!leaveOut.contains(options.at(i)))
                {
                    av_dict_set(&dictionary, optionName(options.at(i)).constData(), options.at(i + 1).toUtf8().constData(), 0);
                }
        }
    return dictionary;
}

// One decoded stream of a file feeding the graph.
struct Feed
{
    int index{-1};
    AVCodecContext *decoder{nullptr};
    AVFilterContext *source{nullptr};
    bool ended{false};          // its source has been told so
    // pictures as the graph's source was told they come: a phone's decoder knows its own form
    // only once it gives one, and what comes otherwise is converted on the way
    AVPixelFormat format{AV_PIX_FMT_NONE};
    int width{0};
    int height{0};
    SwsContext *convert{nullptr};
    AVFrame *converted{nullptr};
};

// A file among the inputs: the video, a piece of music.
struct FileInput
{
    AVFormatContext *format{nullptr};
    double start{0.0};          // -ss: seconds into the file
    double length{-1.0};        // -t; below 0: to its end
    int64_t offset{0};          // where output time 0 lies in the file, in AV_TIME_BASE
    Feed video;
    Feed audio;
    bool ended{false};
};
}
#endif

struct LibavExport::Private
{
    ExportPlan plan;
    DrawGauges draw;
    QString error;
    qint64 sent{0};

#ifdef RTV_LIBAV
    std::vector<FileInput> files;   // by the plan's input index; the pipe's has no format
    AVFilterContext *gaugeSource{nullptr};
    bool gaugesEnded{false};
    bool gaugesEndPushed{false};
    AVFilterGraph *graph{nullptr};
    AVFilterContext *videoSink{nullptr};
    AVFilterContext *audioSink{nullptr};
    AVBufferRef *device{nullptr};
    AVFormatContext *output{nullptr};
    AVCodecContext *videoEncoder{nullptr};
    AVCodecContext *audioEncoder{nullptr};
    AVStream *videoStream{nullptr};
    AVStream *audioStream{nullptr};
    // the video's own sound written as it is
    bool copyAudio{false};
    AVStream *copiedFrom{nullptr};
    bool videoDone{false};
    bool audioDone{true};
    bool finished{false};
    int64_t videoEndUs{-1};         // output time the picture ends at, once it has
    int64_t lastVideoPts{AV_NOPTS_VALUE};
    int64_t lastEncodedPts{AV_NOPTS_VALUE};  // in the encoder's time base
    std::atomic<double> secondsDone{0.0};    // of the picture, read from other threads
    AVPacket *packet{nullptr};
    AVFrame *frame{nullptr};

    ~Private();
    bool fail(const QString &message)
    {
        if (error.isEmpty())
            {
                error = message;
            }
        return false;
    }
    bool fail(const char *what, int code) { return fail(QString("%1: %2").arg(QLatin1String(what), avError(code))); }

    bool openInputs();
    bool openDecoder(FileInput &file, Feed &feed);
    bool buildGraph();
    bool openOutput();
    bool feedFile(FileInput &file);
    bool pushDecoded(FileInput &file, Feed &feed, const AVPacket *packet);
    bool endFeed(Feed &feed);
    bool feedGauges();
    bool drain();
    bool encode(AVCodecContext *encoder, AVStream *stream, AVFrame *frame);
    bool writeCopied(AVPacket *packet, const FileInput &file);
    bool finish();
#endif
};

#ifdef RTV_LIBAV
LibavExport::Private::~Private()
{
    for (FileInput &file : files)
        {
            avcodec_free_context(&file.video.decoder);
            avcodec_free_context(&file.audio.decoder);
            sws_freeContext(file.video.convert);
            av_frame_free(&file.video.converted);
            avformat_close_input(&file.format);
        }
    avfilter_graph_free(&graph);
    avcodec_free_context(&videoEncoder);
    avcodec_free_context(&audioEncoder);
    if (output)
        {
            if (!(output->oformat->flags & AVFMT_NOFILE))
                {
                    avio_closep(&output->pb);
                }
            avformat_free_context(output);
        }
    av_buffer_unref(&device);
    av_packet_free(&packet);
    av_frame_free(&frame);
}

bool LibavExport::Private::openInputs()
{
    for (int i = 0; i < plan.inputs.size(); ++i)
        {
            const ExportPlan::Input &input = plan.inputs.at(i);
            FileInput file;
            if (input.source == QLatin1String(ExportPlan::Pipe))
                {
                    files.push_back(file);
                    continue;
                }
            const int opened = avformat_open_input(&file.format, input.source.toUtf8().constData(), nullptr, nullptr);
            if (opened < 0)
                {
                    return fail(QCoreApplication::translate("LibavExport", "%1 cannot be read: %2").arg(input.source, avError(opened)));
                }
            files.push_back(file);
            FileInput &added = files.back();
            const int found = avformat_find_stream_info(added.format, nullptr);
            if (found < 0)
                {
                    return fail(QCoreApplication::translate("LibavExport", "%1 cannot be read: %2").arg(input.source, avError(found)));
                }
            added.start = valueOf(input.options, "-ss").toDouble();
            const QString length = valueOf(input.options, "-t");
            added.length = length.isEmpty() ? -1.0 : length.toDouble();
            const int64_t fileStart = added.format->start_time == AV_NOPTS_VALUE ? 0 : added.format->start_time;
            added.offset = fileStart + int64_t(added.start * AV_TIME_BASE);
            if (added.start > 0.0)
                {
                    // to the key frame before; what lies before the start is decoded and left out
                    avformat_seek_file(added.format, -1, INT64_MIN, added.offset, added.offset, 0);
                }
            for (unsigned s = 0; s < added.format->nb_streams; ++s)
                {
                    const AVStream *stream = added.format->streams[s];
                    const AVMediaType type = stream->codecpar->codec_type;
                    if (type == AVMEDIA_TYPE_VIDEO && added.video.index < 0 && !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
                        {
                            added.video.index = int(s);
                        }
                    else if (type == AVMEDIA_TYPE_AUDIO && added.audio.index < 0)
                        {
                            added.audio.index = int(s);
                        }
                }
        }
    return true;
}

bool LibavExport::Private::openDecoder(FileInput &file, Feed &feed)
{
    const AVStream *stream = file.format->streams[feed.index];
    auto open = [&](const AVCodec *codec)
    {
        feed.decoder = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(feed.decoder, stream->codecpar);
        feed.decoder->pkt_timebase = stream->time_base;
        feed.decoder->thread_count = 0;
        const int opened = avcodec_open2(feed.decoder, codec, nullptr);
        if (opened < 0)
            {
                avcodec_free_context(&feed.decoder);
            }
        return opened;
    };
    const AVCodec *codec = nullptr;
#ifdef Q_OS_ANDROID
    // the phone's own decoder where it has one for the picture: many times quicker
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            const QByteArray own = QByteArray(avcodec_get_name(stream->codecpar->codec_id)) + "_mediacodec";
            codec = avcodec_find_decoder_by_name(own.constData());
            if (codec && open(codec) < 0)
                {
                    codec = nullptr;
                }
        }
#endif
    if (!feed.decoder)
        {
            codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec)
                {
                    return fail(QCoreApplication::translate("LibavExport", "No decoder for %1").arg(QLatin1String(avcodec_get_name(stream->codecpar->codec_id))));
                }
            const int opened = open(codec);
            if (opened < 0)
                {
                    return fail("Opening a decoder", opened);
                }
        }
    if (feed.decoder->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            qInfo().noquote() << "export: the picture decoded by" << codec->name;
            // the form most phones' decoders give, where it is not known yet
            feed.format = feed.decoder->pix_fmt != AV_PIX_FMT_NONE ? feed.decoder->pix_fmt : AV_PIX_FMT_NV12;
            feed.width = feed.decoder->width;
            feed.height = feed.decoder->height;
        }
    return true;
}

bool LibavExport::Private::buildGraph()
{
    QString text = plan.filterGraph;
    bool audioFromGraph = plan.audioMap == QLatin1String("[a]");
    FileInput *video = files.empty() ? nullptr : &files.front();
    if (plan.audioMap == QLatin1String("0:a?") && video && video->format && video->audio.index >= 0)
        {
            if (valueOf(plan.audioOptions, "-c:a") == QLatin1String("copy"))
                {
                    copyAudio = true;
                    copiedFrom = video->format->streams[video->audio.index];
                }
            else
                {
                    // encoded again as it is: through the graph, which brings it to the encoder's form
                    text += ";[0:a]anull[a]";
                    audioFromGraph = true;
                }
        }

    graph = avfilter_graph_alloc();
    // the graphics card the plan encodes on, which its upload filter needs when it is set up
    const QString vaapi = valueOf(plan.globalOptions, "-vaapi_device");
    if (!vaapi.isEmpty())
        {
            const int made = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, vaapi.toUtf8().constData(), nullptr, 0);
            if (made < 0)
                {
                    return fail("The graphics card (VA-API)", made);
                }
        }
    // in steps, as the ffmpeg program does: the filters are made, given the device, and only then
    // set up (in one go, an upload filter refuses to start without the device)
    AVFilterInOut *inputs = nullptr;
    AVFilterInOut *outputs = nullptr;
    AVFilterGraphSegment *segment = nullptr;
    int result = avfilter_graph_segment_parse(graph, text.toUtf8().constData(), 0, &segment);
    if (result >= 0)
        {
            result = avfilter_graph_segment_create_filters(segment, 0);
        }
    if (result >= 0 && device)
        {
            for (unsigned i = 0; i < graph->nb_filters; ++i)
                {
                    graph->filters[i]->hw_device_ctx = av_buffer_ref(device);
                }
        }
    if (result >= 0)
        {
            result = avfilter_graph_segment_apply_opts(segment, 0);
        }
    if (result >= 0)
        {
            result = avfilter_graph_segment_init(segment, 0);
        }
    if (result >= 0)
        {
            result = avfilter_graph_segment_link(segment, 0, &inputs, &outputs);
        }
    avfilter_graph_segment_free(&segment);
    if (result < 0)
        {
            qWarning().noquote() << "The filter graph that was refused:" << text;
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return fail("The filter graph", result);
        }
    struct Free
    {
        AVFilterInOut **inputs;
        AVFilterInOut **outputs;
        ~Free()
        {
            avfilter_inout_free(inputs);
            avfilter_inout_free(outputs);
        }
    } freeLater{&inputs, &outputs};

    // a source for each input the graph names: "0:v", "1:v", "2:a", "3:a:0" (the first)
    for (AVFilterInOut *in = inputs; in; in = in->next)
        {
            const QString label = QString::fromUtf8(in->name ? in->name : "");
            const int colon = int(label.indexOf(':'));
            const int index = label.left(colon).toInt();
            const bool isAudio = label.mid(colon + 1).startsWith(QLatin1Char('a'));
            if (colon < 1 || index < 0 || index >= int(files.size()))
                {
                    return fail(QString("The filter graph names an unknown input: %1").arg(label));
                }
            FileInput &file = files.at(size_t(index));
            const ExportPlan::Input &input = plan.inputs.at(index);
            QString args;
            AVFilterContext *source = nullptr;
            const QByteArray name = QString("in%1%2").arg(index).arg(isAudio ? "a" : "v").toUtf8();
            if (!file.format)
                {
                    // the gauges the application draws
                    args = QString("video_size=%1:pix_fmt=%2:time_base=%3/%4:pixel_aspect=1/1:frame_rate=%4/%3")
                           .arg(valueOf(input.options, "-video_size"), valueOf(input.options, "-pixel_format"))
                           .arg(plan.fpsDen).arg(plan.fpsNum);
                    result = avfilter_graph_create_filter(&source, avfilter_get_by_name("buffer"), name.constData(),
                                                          args.toUtf8().constData(), nullptr, graph);
                    gaugeSource = source;
                }
            else
                {
                    Feed &feed = isAudio ? file.audio : file.video;
                    if (feed.index < 0)
                        {
                            return fail(isAudio ? QCoreApplication::translate("LibavExport", "%1 has no sound").arg(input.source)
                                        : QCoreApplication::translate("LibavExport", "%1 has no picture").arg(input.source));
                        }
                    if (!openDecoder(file, feed))
                        {
                            return false;
                        }
                    const AVStream *stream = file.format->streams[feed.index];
                    const AVCodecContext *decoder = feed.decoder;
                    if (isAudio)
                        {
                            // zeroed: av_channel_layout_copy frees what the target held before
                            AVChannelLayout layout{};
                            if (decoder->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
                                {
                                    av_channel_layout_default(&layout, decoder->ch_layout.nb_channels);
                                }
                            else
                                {
                                    av_channel_layout_copy(&layout, &decoder->ch_layout);
                                }
                            char described[128] = {};
                            av_channel_layout_describe(&layout, described, sizeof(described));
                            av_channel_layout_uninit(&layout);
                            args = QString("time_base=%1/%2:sample_rate=%3:sample_fmt=%4:channel_layout=%5")
                                   .arg(stream->time_base.num).arg(stream->time_base.den).arg(decoder->sample_rate)
                                   .arg(QLatin1String(av_get_sample_fmt_name(decoder->sample_fmt)), QLatin1String(described));
                            result = avfilter_graph_create_filter(&source, avfilter_get_by_name("abuffer"), name.constData(),
                                                                  args.toUtf8().constData(), nullptr, graph);
                        }
                    else
                        {
                            const AVRational aspect = decoder->sample_aspect_ratio.num > 0 ? decoder->sample_aspect_ratio
                                                      : AVRational{1, 1};
                            const AVRational rate = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate : stream->r_frame_rate;
                            args = QString("video_size=%1x%2:pix_fmt=%3:time_base=%4/%5:pixel_aspect=%6/%7")
                                   .arg(feed.width).arg(feed.height).arg(int(feed.format))
                                   .arg(stream->time_base.num).arg(stream->time_base.den).arg(aspect.num).arg(aspect.den);
                            if (rate.num > 0 && rate.den > 0)
                                {
                                    args += QString(":frame_rate=%1/%2").arg(rate.num).arg(rate.den);
                                }
                            result = avfilter_graph_create_filter(&source, avfilter_get_by_name("buffer"), name.constData(),
                                                                  args.toUtf8().constData(), nullptr, graph);
                        }
                    feed.source = source;
                }
            if (result < 0)
                {
                    return fail("An input of the filter graph", result);
                }
            // a video's picture turned as it is shown before the graph gets it
            AVFilterContext *last = source;
            if (file.format && !isAudio)
                {
                    int turn = 0;
                    for (const auto &[filter, filterArgs] : uprightFilters(file.format->streams[file.video.index]))
                        {
                            AVFilterContext *added = nullptr;
                            const QByteArray turnName = name + "turn" + QByteArray::number(++turn);
                            result = avfilter_graph_create_filter(&added, avfilter_get_by_name(filter), turnName.constData(),
                                                                  filterArgs.isEmpty() ? nullptr : filterArgs.toUtf8().constData(),
                                                                  nullptr, graph);
                            if (result >= 0)
                                {
                                    result = avfilter_link(last, 0, added, 0);
                                }
                            if (result < 0)
                                {
                                    return fail("Turning the picture upright", result);
                                }
                            last = added;
                        }
                }
            result = avfilter_link(last, 0, in->filter_ctx, unsigned(in->pad_idx));
            if (result < 0)
                {
                    return fail("Linking an input of the filter graph", result);
                }
        }

    // the outputs: [v] to the picture's encoder, [a] to the sound's
    const QString pixelFormat = valueOf(plan.videoOptions, "-pix_fmt");
    const AVCodec *audioCodec = audioFromGraph ? avcodec_find_encoder_by_name(valueOf(plan.audioOptions, "-c:a").toUtf8().constData())
                                : nullptr;
    if (audioFromGraph && !audioCodec)
        {
            return fail(QCoreApplication::translate("LibavExport", "No sound encoder %1 in this build")
                        .arg(valueOf(plan.audioOptions, "-c:a")));
        }
    for (AVFilterInOut *out = outputs; out; out = out->next)
        {
            const QString label = QString::fromUtf8(out->name ? out->name : "");
            AVFilterContext *last = out->filter_ctx;
            unsigned pad = unsigned(out->pad_idx);
            auto append = [&](const char *filter, const QString &args, const char *name)
            {
                AVFilterContext *added = nullptr;
                int made = avfilter_graph_create_filter(&added, avfilter_get_by_name(filter), name,
                                                        args.isEmpty() ? nullptr : args.toUtf8().constData(), nullptr, graph);
                if (made >= 0)
                    {
                        made = avfilter_link(last, pad, added, 0);
                    }
                last = added;
                pad = 0;
                return made;
            };
            if (label == QLatin1String("v"))
                {
                    if (!pixelFormat.isEmpty() && (result = append("format", "pix_fmts=" + pixelFormat, "outformat")) < 0)
                        {
                            return fail("The picture's format", result);
                        }
                    if ((result = append("buffersink", QString(), "outv")) < 0)
                        {
                            return fail("The picture's output", result);
                        }
                    videoSink = last;
                }
            else if (label == QLatin1String("a"))
                {
                    // the sample format and rates the encoder takes
                    QStringList formats;
                    QStringList rates;
                    const void *list = nullptr;
                    int count = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
                    avcodec_get_supported_config(nullptr, audioCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &list, &count);
                    for (int i = 0; i < count; ++i)
                        formats << QLatin1String(av_get_sample_fmt_name(static_cast<const AVSampleFormat *>(list)[i]));
                    avcodec_get_supported_config(nullptr, audioCodec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &list, &count);
                    for (int i = 0; i < count; ++i)
                        rates << QString::number(static_cast<const int *>(list)[i]);
#else
                    for (const AVSampleFormat *f = audioCodec->sample_fmts; f && *f != AV_SAMPLE_FMT_NONE; ++f)
                        formats << QLatin1String(av_get_sample_fmt_name(*f));
                    for (const int *r = audioCodec->supported_samplerates; r && *r; ++r)
                        rates << QString::number(*r);
                    Q_UNUSED(list);
                    Q_UNUSED(count);
#endif
                    QStringList args;
                    if (!formats.isEmpty())
                        args << "sample_fmts=" + formats.join('|');
                    if (!rates.isEmpty())
                        args << "sample_rates=" + rates.join('|');
                    if (!args.isEmpty() && (result = append("aformat", args.join(':'), "outaformat")) < 0)
                        {
                            return fail("The sound's format", result);
                        }
                    if ((result = append("abuffersink", QString(), "outa")) < 0)
                        {
                            return fail("The sound's output", result);
                        }
                    audioSink = last;
                }
            else
                {
                    return fail(QString("The filter graph has an output nobody takes: %1").arg(label));
                }
        }
    if (!videoSink)
        {
            return fail(QString("The filter graph has no picture output"));
        }

    result = avfilter_graph_config(graph, nullptr);
    if (result < 0)
        {
            return fail("Setting up the filter graph", result);
        }
    audioDone = !audioSink && !copyAudio;
    return true;
}

bool LibavExport::Private::openOutput()
{
    const QByteArray path = plan.outputPath.toUtf8();
    int result = avformat_alloc_output_context2(&output, nullptr, nullptr, path.constData());
    if (result < 0 || !output)
        {
            return fail("The output file's type", result);
        }
    const bool globalHeader = output->oformat->flags & AVFMT_GLOBALHEADER;

    // the picture
    const QString videoName = valueOf(plan.videoOptions, "-c:v");
    const AVCodec *videoCodec = avcodec_find_encoder_by_name(videoName.toUtf8().constData());
    if (!videoCodec)
        {
            return fail(QCoreApplication::translate("LibavExport", "No encoder %1 in this build").arg(videoName));
        }
    videoEncoder = avcodec_alloc_context3(videoCodec);
    videoEncoder->width = av_buffersink_get_w(videoSink);
    videoEncoder->height = av_buffersink_get_h(videoSink);
    videoEncoder->pix_fmt = AVPixelFormat(av_buffersink_get_format(videoSink));
    videoEncoder->sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(videoSink);
    videoEncoder->framerate = av_buffersink_get_frame_rate(videoSink);
    // a tick a frame, as ffmpeg encodes: the graph's own clock may be finer (concat's is in
    // microseconds), and a container keeps the encoder's
    videoEncoder->time_base = videoEncoder->framerate.num > 0 && videoEncoder->framerate.den > 0
                              ? av_inv_q(videoEncoder->framerate) : av_buffersink_get_time_base(videoSink);
    if (AVBufferRef *frames = av_buffersink_get_hw_frames_ctx(videoSink))
        {
            videoEncoder->hw_frames_ctx = av_buffer_ref(frames);
        }
    if (globalHeader)
        {
            videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
    AVDictionary *videoOptions = dictionaryOf(plan.videoOptions, {"-c:v", "-pix_fmt"});
    result = avcodec_open2(videoEncoder, videoCodec, &videoOptions);
    av_dict_free(&videoOptions);
    if (result < 0)
        {
            return fail(QString("Opening the encoder %1").arg(videoName).toUtf8().constData(), result);
        }
    videoStream = avformat_new_stream(output, nullptr);
    avcodec_parameters_from_context(videoStream->codecpar, videoEncoder);
    videoStream->time_base = videoEncoder->time_base;
    videoStream->avg_frame_rate = videoEncoder->framerate;

    // the sound, encoded from the graph or copied
    if (audioSink)
        {
            const QString audioName = valueOf(plan.audioOptions, "-c:a");
            const AVCodec *audioCodec = avcodec_find_encoder_by_name(audioName.toUtf8().constData());
            audioEncoder = avcodec_alloc_context3(audioCodec);
            audioEncoder->sample_rate = av_buffersink_get_sample_rate(audioSink);
            audioEncoder->sample_fmt = AVSampleFormat(av_buffersink_get_format(audioSink));
            av_buffersink_get_ch_layout(audioSink, &audioEncoder->ch_layout);
            audioEncoder->time_base = AVRational{1, audioEncoder->sample_rate};
            if (globalHeader)
                {
                    audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                }
            AVDictionary *audioOptions = dictionaryOf(plan.audioOptions, {"-c:a"});
            result = avcodec_open2(audioEncoder, audioCodec, &audioOptions);
            av_dict_free(&audioOptions);
            if (result < 0)
                {
                    return fail(QString("Opening the encoder %1").arg(audioName).toUtf8().constData(), result);
                }
            if (!(audioCodec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) && audioEncoder->frame_size > 0)
                {
                    av_buffersink_set_frame_size(audioSink, unsigned(audioEncoder->frame_size));
                }
            audioStream = avformat_new_stream(output, nullptr);
            avcodec_parameters_from_context(audioStream->codecpar, audioEncoder);
            audioStream->time_base = audioEncoder->time_base;
        }
    else if (copyAudio)
        {
            audioStream = avformat_new_stream(output, nullptr);
            avcodec_parameters_copy(audioStream->codecpar, copiedFrom->codecpar);
            audioStream->codecpar->codec_tag = 0;
            audioStream->time_base = copiedFrom->time_base;
        }

    // the container's own options, and the tag Apple players want for HEVC
    const QString tag = valueOf(plan.muxerArguments, "-tag:v");
    if (tag.size() == 4)
        {
            const QByteArray t = tag.toLatin1();
            videoStream->codecpar->codec_tag = MKTAG(t[0], t[1], t[2], t[3]);
        }
    if (!(output->oformat->flags & AVFMT_NOFILE))
        {
            result = avio_open(&output->pb, path.constData(), AVIO_FLAG_WRITE);
            if (result < 0)
                {
                    return fail(QCoreApplication::translate("LibavExport", "%1 cannot be written: %2")
                                .arg(plan.outputPath, avError(result)));
                }
        }
    AVDictionary *muxerOptions = dictionaryOf(plan.muxerArguments, {"-tag:v"});
    result = avformat_write_header(output, &muxerOptions);
    av_dict_free(&muxerOptions);
    return result < 0 ? fail("Starting the output file", result) : true;
}

bool LibavExport::Private::endFeed(Feed &feed)
{
    if (feed.ended || !feed.source)
        {
            feed.ended = true;
            return true;
        }
    feed.ended = true;
    const int result = av_buffersrc_add_frame(feed.source, nullptr);
    return result < 0 && result != AVERROR_EOF ? fail("Ending an input", result) : true;
}

bool LibavExport::Private::pushDecoded(FileInput &file, Feed &feed, const AVPacket *packet)
{
    int result = avcodec_send_packet(feed.decoder, packet);
    if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        {
            // a damaged packet is left out, as ffmpeg does
            return true;
        }
    const AVRational base = file.format->streams[feed.index]->time_base;
    const int64_t offset = av_rescale_q(file.offset, AV_TIME_BASE_Q, base);
    while ((result = avcodec_receive_frame(feed.decoder, frame)) >= 0)
        {
            const int64_t at = frame->best_effort_timestamp;
            if (feed.ended || at == AV_NOPTS_VALUE)
                {
                    av_frame_unref(frame);
                    continue;
                }
            const int64_t pts = at - offset;
            const double seconds = pts * av_q2d(base);
            // what the seek brought in before the start
            const int64_t length = feed.decoder->codec_type == AVMEDIA_TYPE_AUDIO && frame->sample_rate > 0
                                   ? av_rescale_q(frame->nb_samples, AVRational{1, frame->sample_rate}, base) : 0;
            if (pts < 0 && pts + length <= 0)
                {
                    av_frame_unref(frame);
                    continue;
                }
            if (file.length >= 0.0 && seconds >= file.length)
                {
                    av_frame_unref(frame);
                    if (!endFeed(feed))
                        {
                            return false;
                        }
                    continue;
                }
            AVFrame *pushed = frame;
            if (feed.format != AV_PIX_FMT_NONE
                && (frame->format != feed.format || frame->width != feed.width || frame->height != feed.height))
                {
                    feed.convert = sws_getCachedContext(feed.convert, frame->width, frame->height, AVPixelFormat(frame->format),
                                                        feed.width, feed.height, feed.format, SWS_BILINEAR,
                                                        nullptr, nullptr, nullptr);
                    if (!feed.converted)
                        {
                            feed.converted = av_frame_alloc();
                        }
                    feed.converted->format = feed.format;
                    feed.converted->width = feed.width;
                    feed.converted->height = feed.height;
                    result = feed.convert ? sws_scale_frame(feed.convert, feed.converted, frame) : AVERROR(EINVAL);
                    if (result < 0)
                        {
                            av_frame_unref(frame);
                            av_frame_unref(feed.converted);
                            return fail("Converting a decoded picture", result);
                        }
                    av_frame_copy_props(feed.converted, frame);
                    pushed = feed.converted;
                }
            pushed->pts = pts;
            result = av_buffersrc_add_frame(feed.source, pushed);
            av_frame_unref(frame);
            av_frame_unref(feed.converted);
            if (result < 0 && result != AVERROR_EOF)
                {
                    return fail("Feeding the filter graph", result);
                }
        }
    return true;
}

bool LibavExport::Private::writeCopied(AVPacket *copied, const FileInput &file)
{
    if (audioDone)
        {
            return true;
        }
    const AVRational base = copiedFrom->time_base;
    const int64_t offset = av_rescale_q(file.offset, AV_TIME_BASE_Q, base);
    if (copied->pts == AV_NOPTS_VALUE)
        {
            return true;
        }
    copied->pts -= offset;
    if (copied->dts != AV_NOPTS_VALUE)
        {
            copied->dts -= offset;
        }
    if (copied->pts < 0)
        {
            return true;
        }
    const int64_t atUs = av_rescale_q(copied->pts, base, AV_TIME_BASE_Q);
    const bool pastLength = file.length >= 0.0 && atUs >= int64_t(file.length * AV_TIME_BASE);
    // the sound ends with the picture, also where the user stopped
    if (pastLength || (videoDone && atUs >= videoEndUs))
        {
            audioDone = true;
            return true;
        }
    copied->stream_index = audioStream->index;
    av_packet_rescale_ts(copied, base, audioStream->time_base);
    copied->pos = -1;
    const int result = av_interleaved_write_frame(output, copied);
    return result < 0 ? fail("Writing the sound", result) : true;
}

bool LibavExport::Private::feedFile(FileInput &file)
{
    const int result = av_read_frame(file.format, packet);
    if (result < 0)
        {
            // the end: what the decoders still hold, then the end itself
            file.ended = true;
            for (Feed *feed : {&file.video, &file.audio})
                {
                    if (feed->decoder && !feed->ended)
                        {
                            if (!pushDecoded(file, *feed, nullptr) || !endFeed(*feed))
                                {
                                    return false;
                                }
                        }
                }
            if (copyAudio && &file == &files.front())
                {
                    audioDone = true;
                }
            return true;
        }
    bool ok = true;
    if (packet->stream_index == file.video.index && file.video.decoder && !file.video.ended && !videoDone)
        {
            ok = pushDecoded(file, file.video, packet);
        }
    else if (packet->stream_index == file.audio.index && file.audio.decoder && !file.audio.ended)
        {
            ok = pushDecoded(file, file.audio, packet);
        }
    else if (copyAudio && &file == &files.front() && packet->stream_index == file.audio.index)
        {
            ok = writeCopied(packet, file);
        }
    av_packet_unref(packet);
    return ok;
}

bool LibavExport::Private::feedGauges()
{
    if (gaugesEnded || sent >= plan.frameCount)
        {
            if (gaugesEndPushed)
                {
                    return true;
                }
            gaugesEndPushed = true;
            const int result = av_buffersrc_add_frame(gaugeSource, nullptr);
            return result < 0 && result != AVERROR_EOF ? fail("Ending the gauges", result) : true;
        }
    const QImage picture = draw(sent);
    AVFrame *gauges = av_frame_alloc();
    gauges->width = picture.width();
    gauges->height = picture.height();
    for (const ExportPlan::Input &input : plan.inputs)
        {
            if (input.source == QLatin1String(ExportPlan::Pipe))
                {
                    gauges->format = av_get_pix_fmt(valueOf(input.options, "-pixel_format").toLatin1().constData());
                }
        }
    int result = av_frame_get_buffer(gauges, 0);
    if (result < 0)
        {
            av_frame_free(&gauges);
            return fail("A picture of the gauges", result);
        }
    const int rowBytes = picture.width() * 4;
    for (int y = 0; y < picture.height(); ++y)
        {
            memcpy(gauges->data[0] + y * gauges->linesize[0], picture.constScanLine(y), size_t(rowBytes));
        }
    gauges->pts = sent;
    ++sent;
    result = av_buffersrc_add_frame(gaugeSource, gauges);
    av_frame_free(&gauges);
    return result < 0 && result != AVERROR_EOF ? fail("Feeding the gauges", result) : true;
}

bool LibavExport::Private::encode(AVCodecContext *encoder, AVStream *stream, AVFrame *raw)
{
    int result = avcodec_send_frame(encoder, raw);
    if (result < 0 && result != AVERROR_EOF)
        {
            return fail("Encoding", result);
        }
    while ((result = avcodec_receive_packet(encoder, packet)) >= 0)
        {
            // a picture lasts a frame, also where the graph did not say (after concat): else the
            // container guesses the last one's length, and the rate read back is a little off
            if (packet->duration <= 0 && encoder->codec_type == AVMEDIA_TYPE_VIDEO && encoder->framerate.num > 0)
                {
                    packet->duration = av_rescale_q(1, av_inv_q(encoder->framerate), encoder->time_base);
                }
            av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
            packet->stream_index = stream->index;
            result = av_interleaved_write_frame(output, packet);
            if (result < 0)
                {
                    return fail("Writing the file", result);
                }
        }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? true : fail("Encoding", result);
}

bool LibavExport::Private::drain()
{
    int result = 0;
    if (!videoDone)
        {
            const AVRational base = av_buffersink_get_time_base(videoSink);
            while ((result = av_buffersink_get_frame_flags(videoSink, frame, AV_BUFFERSINK_FLAG_NO_REQUEST)) >= 0)
                {
                    lastVideoPts = frame->pts;
                    if (frame->pts != AV_NOPTS_VALUE)
                        {
                            secondsDone = std::max(0.0, frame->pts * av_q2d(base));
                        }
                    frame->pict_type = AV_PICTURE_TYPE_NONE;
                    if (frame->pts != AV_NOPTS_VALUE)
                        {
                            frame->pts = av_rescale_q(frame->pts, base, videoEncoder->time_base);
                            // two pictures of a variable rate on one tick: the later goes, as ffmpeg drops it
                            if (lastEncodedPts != AV_NOPTS_VALUE && frame->pts <= lastEncodedPts)
                                {
                                    av_frame_unref(frame);
                                    continue;
                                }
                            lastEncodedPts = frame->pts;
                        }
                    frame->duration = 0;
                    const bool ok = encode(videoEncoder, videoStream, frame);
                    av_frame_unref(frame);
                    if (!ok)
                        {
                            return false;
                        }
                }
            if (result == AVERROR_EOF)
                {
                    videoDone = true;
                    // the end of the last picture
                    const int64_t step = av_rescale_q(1, av_inv_q(av_buffersink_get_frame_rate(videoSink)), base);
                    videoEndUs = lastVideoPts == AV_NOPTS_VALUE ? 0 : av_rescale_q(lastVideoPts + std::max<int64_t>(step, 1), base,
                                 AV_TIME_BASE_Q);
                    if (!encode(videoEncoder, videoStream, nullptr))
                        {
                            return false;
                        }
                }
            else if (result != AVERROR(EAGAIN))
                {
                    return fail("The filter graph's picture", result);
                }
        }
    if (audioSink && !audioDone)
        {
            const AVRational base = av_buffersink_get_time_base(audioSink);
            while (!audioDone && (result = av_buffersink_get_frame_flags(audioSink, frame, AV_BUFFERSINK_FLAG_NO_REQUEST)) >= 0)
                {
                    const int64_t atUs = av_rescale_q(frame->pts, base, AV_TIME_BASE_Q);
                    if (videoDone && atUs >= videoEndUs)
                        {
                            // the sound ends with the picture
                            audioDone = true;
                            av_frame_unref(frame);
                            break;
                        }
                    frame->pts = av_rescale_q(frame->pts, base, audioEncoder->time_base);
                    const bool ok = encode(audioEncoder, audioStream, frame);
                    av_frame_unref(frame);
                    if (!ok)
                        {
                            return false;
                        }
                }
            if (result == AVERROR_EOF)
                {
                    audioDone = true;
                }
            else if (!audioDone && result < 0 && result != AVERROR(EAGAIN))
                {
                    return fail("The filter graph's sound", result);
                }
            if (audioDone && !encode(audioEncoder, audioStream, nullptr))
                {
                    return false;
                }
        }
    return true;
}

bool LibavExport::Private::finish()
{
    finished = true;
    const int result = av_write_trailer(output);
    if (result < 0)
        {
            return fail("Finishing the file", result);
        }
    if (!(output->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&output->pb);
        }
    return true;
}
#endif

LibavExport::LibavExport(const ExportPlan &plan, DrawGauges draw)
    : d(std::make_unique<Private>())
{
    d->plan = plan;
    d->draw = std::move(draw);
}

LibavExport::~LibavExport() = default;

bool LibavExport::available()
{
#ifdef RTV_LIBAV
    return true;
#else
    return false;
#endif
}

bool LibavExport::start(QString *error)
{
#ifdef RTV_LIBAV
    Libav::quiet();
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    const bool ok = d->openInputs() && d->buildGraph() && d->openOutput();
    if (!ok && error)
        {
            *error = d->error;
        }
    return ok;
#else
    if (error)
        {
            *error = QCoreApplication::translate("LibavExport", "Built without FFmpeg's libraries");
        }
    return false;
#endif
}

LibavExport::State LibavExport::step()
{
#ifdef RTV_LIBAV
    if (d->finished)
        {
            return Done;
        }
    if (!d->error.isEmpty())
        {
            return Failed;
        }
    // a slice: until a picture of the gauges has gone in, or some rounds without one
    const qint64 sentBefore = d->sent;
    for (int round = 0; round < 64 && d->sent == sentBefore; ++round)
        {
            if (!d->drain())
                {
                    return Failed;
                }
            if (d->videoDone && d->audioDone)
                {
                    return d->finish() ? Done : Failed;
                }
            if (d->videoDone && d->copyAudio && !d->audioDone)
                {
                    // the picture has ended: only the copied sound up to its end is still to come
                    if (d->files.front().ended)
                        {
                            d->audioDone = true;
                        }
                    else if (!d->feedFile(d->files.front()))
                        {
                            return Failed;
                        }
                    continue;
                }
            const int result = avfilter_graph_request_oldest(d->graph);
            if (result == AVERROR_EOF)
                {
                    // every output has ended: what is left comes out at the next drain
                    if (!d->drain())
                        {
                            return Failed;
                        }
                    d->videoDone = true;
                    if (d->audioSink && !d->audioDone)
                        {
                            d->audioDone = true;
                            if (!d->encode(d->audioEncoder, d->audioStream, nullptr))
                                {
                                    return Failed;
                                }
                        }
                    // copied sound still to come from the video's file: in the next rounds
                    if (!d->audioDone && d->copyAudio)
                        {
                            continue;
                        }
                    return d->finish() ? Done : Failed;
                }
            if (result < 0 && result != AVERROR(EAGAIN))
                {
                    d->fail("The filter graph", result);
                    return Failed;
                }
            if (result >= 0)
                {
                    continue;
                }
            // the input the graph waits for most: the gauges, or the next piece of a file
            AVFilterContext *hungriest = nullptr;
            unsigned most = 0;
            auto consider = [&](AVFilterContext *source, bool ended)
            {
                if (source && !ended)
                    {
                        const unsigned failed = av_buffersrc_get_nb_failed_requests(source);
                        if (!hungriest || failed > most)
                            {
                                hungriest = source;
                                most = failed;
                            }
                    }
            };
            consider(d->gaugeSource, d->gaugesEndPushed);
            for (FileInput &file : d->files)
                {
                    consider(file.video.source, file.video.ended || file.ended);
                    consider(file.audio.source, file.audio.ended || file.ended);
                }
            bool ok = true;
            if (hungriest && hungriest == d->gaugeSource)
                {
                    ok = d->feedGauges();
                }
            else if (hungriest)
                {
                    for (FileInput &file : d->files)
                        {
                            if (file.video.source == hungriest || file.audio.source == hungriest)
                                {
                                    ok = d->feedFile(file);
                                    break;
                                }
                        }
                }
            else if (d->copyAudio && !d->audioDone)
                {
                    d->audioDone = true;
                }
            if (!ok)
                {
                    return Failed;
                }
        }
    return Working;
#else
    return Failed;
#endif
}

void LibavExport::endGauges()
{
#ifdef RTV_LIBAV
    d->gaugesEnded = true;
#endif
}

qint64 LibavExport::gaugesSent() const
{
    return d->sent;
}

double LibavExport::secondsDone() const
{
#ifdef RTV_LIBAV
    return d->secondsDone;
#else
    return 0.0;
#endif
}

QString LibavExport::error() const
{
    return d->error;
}
