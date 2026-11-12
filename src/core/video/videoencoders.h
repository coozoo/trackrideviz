#pragma once

#include "exportsettings.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVector>

// One way of compressing the picture that ffmpeg may offer.
struct VideoEncoder
{
    QString id;              // ffmpeg's name: "libx264", "h264_vaapi"
    QString label;           // for the user
    bool hardware{false};
    bool vaapi{false};       // frames have to be uploaded to a VA-API surface first
    bool hasQuality{true};   // has a constant-quality mode; otherwise bitrate only
    bool hasSpeed{false};    // understands -preset ultrafast..veryslow
    bool hevc{false};
    int defaultQuality{23};  // a sensible start for the constant-quality mode
};

class VideoEncoders
{
    Q_DECLARE_TR_FUNCTIONS(VideoEncoders)

public:
    // Everything the application knows how to drive, best choice first
    // within software and within hardware.
    static QVector<VideoEncoder> known();
    // The entry for `id`; libx264 when the id is not known.
    static VideoEncoder byId(const QString &id);

    // The ones this ffmpeg on this computer can really use. A hardware
    // encoder counts only if a tiny test encode succeeds: being compiled into
    // ffmpeg says nothing about the graphics card. Takes about a second;
    // call from a worker thread.
    static QVector<VideoEncoder> detect(const QString &ffmpegProgram);

    // "-c:v ..." with rate control and speed for these settings.
    static QStringList arguments(const VideoEncoder &encoder, const ExportSettings &settings);
    // Options that must come before the inputs (device set-up).
    static QStringList globalArguments(const VideoEncoder &encoder);
    // What the end of the filter chain has to be for this encoder.
    static QString outputFilter(const VideoEncoder &encoder);
};
