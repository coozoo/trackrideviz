#pragma once

#include <QString>

// The choices of one export. The last used ones are remembered in AppSettings.
struct ExportSettings
{
    enum RateControl { Quality, Bitrate };
    enum Speed { Fast, Balanced, Small };          // encoding speed against file size
    enum Audio { CopyAudio, AacAudio, NoAudio };

    QString outputPath;                 // the container follows from the suffix (.mp4, .mkv, .mov)
    QString encoder{QStringLiteral("libx264")}; // ffmpeg encoder name, see VideoEncoders
    // Bitrate by default: the export dialog sets it to the bitrate of the
    // source. Constant quality on shaky action camera footage easily gives
    // files several times bigger than the original, for nothing.
    RateControl rateControl{Bitrate};
    int quality{23};                    // encoder's own scale, lower is better (CRF / QP)
    int bitrateKbps{20000};
    Speed speed{Balanced};
    int height{0};                      // output height; 0 = same as the source
    Audio audio{CopyAudio};

    double start{0.0};                  // seconds into the video
    double duration{0.0};               // 0 = to the end
};
