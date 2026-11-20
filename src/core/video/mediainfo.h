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
    int audioSampleRate{0};
    int audioChannels{0};
    // How the picture's decoder is set up (SHA-256 of the codec's extradata: H.264's SPS and
    // PPS), lowercase hex; empty when there is none. Files of one encoder and its settings
    // share it; a copy joined across a difference decodes the later parts as garbage.
    QString videoSetup;
    // the clocks the picture and the sound are stored in ("1/15360", "1/48000"; Matroska's are
    // "1/1000"): ffmpeg's concat copies only files whose clocks are the same
    QString videoTimeBase;
    QString audioTimeBase;
    bool hasCameraData{false};   // a GoPro's GPMF track ("gpmd"): positions, G-forces
    // when the camera says it recorded the file (creation_time); cameras
    // differ in whether that is the start or the end, and in the time zone
    QDateTime created;

    bool isValid() const { return !size.isEmpty() && fpsNum > 0 && fpsDen > 0; }
    double fps() const { return fpsDen > 0 ? double(fpsNum) / fpsDen : 0.0; }
};

// Reads MediaInfo: with the program's own libraries when it has them (Libav), or with
// ffprobe, the companion of ffmpeg.
class MediaProbe
{
    Q_DECLARE_TR_FUNCTIONS(MediaProbe)

public:
    // Reads the file, or runs ffprobe and waits for it; call from a worker thread. The engine
    // of the settings first (`preferOwn`: the libraries, else ffprobe), the other where it fails.
    static bool probe(const QString &ffprobeProgram, const QString &videoPath, MediaInfo *info, QString *error = nullptr,
                      bool preferOwn = true);
    // With ffprobe alone.
    static bool probeWithFfprobe(const QString &ffprobeProgram, const QString &videoPath, MediaInfo *info,
                                 QString *error = nullptr);

    // Interprets the output of `ffprobe -print_format json -show_format -show_streams`.
    static bool parse(const QByteArray &ffprobeJson, MediaInfo *info, QString *error = nullptr);
};
