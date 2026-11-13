#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QSize>
#include <QString>

// What the export needs to know about a video file.
struct MediaInfo
{
    QSize size;               // as displayed: rotation metadata already applied
    int fpsNum{0};            // frame rate as the exact fraction ffmpeg reports (60000/1001)
    int fpsDen{1};
    double duration{0.0};     // seconds
    qint64 frameCount{0};     // exact number of frames when the container states it; 0 = unknown
    int bitrateKbps{0};       // of the picture alone when known, else of the whole file; 0 = unknown
    QString videoCodec;
    bool hasAudio{false};
    QString audioCodec;
    // when the camera says it recorded the file (creation_time); cameras
    // differ in whether that is the start or the end, and in the time zone
    QDateTime created;

    bool isValid() const { return !size.isEmpty() && fpsNum > 0 && fpsDen > 0; }
    double fps() const { return fpsDen > 0 ? double(fpsNum) / fpsDen : 0.0; }
};

// Reads MediaInfo with ffprobe, the companion of ffmpeg.
class MediaProbe
{
    Q_DECLARE_TR_FUNCTIONS(MediaProbe)

public:
    // Runs ffprobe and waits for it; call from a worker thread.
    static bool probe(const QString &ffprobeProgram, const QString &videoPath, MediaInfo *info, QString *error = nullptr);

    // Interprets the output of `ffprobe -print_format json -show_format -show_streams`.
    static bool parse(const QByteArray &ffprobeJson, MediaInfo *info, QString *error = nullptr);
};
