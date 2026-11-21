#pragma once

#include <QColor>
#include <QFileInfo>
#include <QString>

#include <iterator>

// The choices of one export. The last used ones are remembered in AppSettings.
struct ExportSettings
{
    enum RateControl { Quality, Bitrate };
    enum Speed { Fast, Balanced, Small };          // encoding speed against file size
    // Opus and Vorbis are kept in Matroska: MP4 and MOV cannot hold Vorbis, and most players no Opus there.
    enum Audio { CopyAudio, AacAudio, NoAudio, OpusAudio, VorbisAudio };
    // The gauges alone, on a transparent background, are for laying over the video in
    // another editor: ProRes 4444 in .mov, VP9 with transparency in .webm (or .mkv). No sound.
    enum Picture { VideoWithGauges, GaugesOnly };

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
    int audioKbps{192};                 // when the sound is encoded again
    Picture picture{VideoWithGauges};   // not remembered: every export starts with the video
    // with no video (ExportPlan::make with no path): what the gauges are drawn over
    QColor background{Qt::black};

    static bool needsMatroska(Audio audio) { return audio == OpusAudio || audio == VorbisAudio; }
    // MP4 and its kin, which cannot hold Opus or Vorbis.
    static bool isMp4Family(const QString &suffix)
    {
        const QString lower = suffix.toLower();
        return lower == QLatin1String("mp4") || lower == QLatin1String("mov") || lower == QLatin1String("m4v");
    }
    // The file types of the gauges alone; ProRes for any other.
    static bool isVp9Alpha(const QString &suffix)
    {
        const QString lower = suffix.toLower();
        return lower == QLatin1String("webm") || lower == QLatin1String("mkv");
    }
    // `path` with its extension, which ffmpeg picks the container by, made `suffix`.
    static QString withSuffix(const QString &path, const QString &suffix)
    {
        const QString current = QFileInfo(path).suffix();
        if (path.isEmpty() || current.compare(suffix, Qt::CaseInsensitive) == 0)
            {
                return path;
            }
        return current.isEmpty() ? path + "." + suffix : path.left(path.size() - current.size()) + suffix;
    }

    // The words the choices are stored as, in the order of their enums (settings, profiles).
    static constexpr const char *RateControlWords[] = {"quality", "bitrate"};
    static constexpr const char *SpeedWords[] = {"fast", "balanced", "small"};
    static constexpr const char *AudioWords[] = {"copy", "aac", "none", "opus", "vorbis"};

    double start{0.0};                  // seconds into the video
    double duration{0.0};               // 0 = to the end
};

static_assert(std::size(ExportSettings::RateControlWords) == ExportSettings::Bitrate + 1, "a word for every rate control");
static_assert(std::size(ExportSettings::SpeedWords) == ExportSettings::Small + 1, "a word for every speed");
static_assert(std::size(ExportSettings::AudioWords) == ExportSettings::VorbisAudio + 1, "a word for every sound");
