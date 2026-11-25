#include "exportprofile.h"

#include "exportplan.h"

#include <QFileInfo>

#include <algorithm>

namespace
{
template <typename Enum, int N>
Enum enumOf(const QString &word, const char *const (&words)[N], Enum fallback)
{
    for (int i = 0; i < N; ++i)
        {
            if (word == QLatin1String(words[i]))
                {
                    return Enum(i);
                }
        }
    return fallback;
}
}

QJsonObject ExportProfile::toJson() const
{
    // only the user's own are stored; their bitrate is fixed
    return QJsonObject
    {
        {"name", name},
        {"encoder", encoder},
        {"rateControl", ExportSettings::RateControlWords[rateControl]},
        {"quality", quality},
        {"bitrateKbps", bitrateKbps},
        {"speed", ExportSettings::SpeedWords[speed]},
        {"height", height},
        {"audio", ExportSettings::AudioWords[audio]},
        {"audioKbps", audioKbps},
        {"suffix", suffix},
    };
}

ExportProfile ExportProfile::fromJson(const QJsonObject &json)
{
    ExportProfile profile;
    profile.name = json.value("name").toString();
    profile.id = profile.name;
    profile.encoder = json.value("encoder").toString(QStringLiteral("libx264"));
    profile.rateControl = enumOf(json.value("rateControl").toString(), ExportSettings::RateControlWords, ExportSettings::Bitrate);
    profile.quality = json.value("quality").toInt(23);
    profile.bitrateKbps = json.value("bitrateKbps").toInt(20000);
    profile.speed = enumOf(json.value("speed").toString(), ExportSettings::SpeedWords, ExportSettings::Balanced);
    profile.height = json.value("height").toInt(0);
    profile.audio = enumOf(json.value("audio").toString(), ExportSettings::AudioWords, ExportSettings::CopyAudio);
    profile.audioKbps = json.value("audioKbps").toInt(192);
    profile.suffix = json.value("suffix").toString(QStringLiteral("mp4"));
    return profile;
}

ExportProfile ExportProfile::of(const QString &name, const ExportSettings &settings)
{
    ExportProfile profile;
    profile.id = name;
    profile.name = name;
    profile.encoder = settings.encoder;
    profile.rateControl = settings.rateControl;
    profile.quality = settings.quality;
    profile.bitrateKbps = settings.bitrateKbps;
    profile.speed = settings.speed;
    profile.height = settings.height;
    profile.audio = settings.audio;
    profile.audioKbps = settings.audioKbps;
    const QString suffix = QFileInfo(settings.outputPath).suffix().toLower();
    profile.suffix = suffix.isEmpty() ? QStringLiteral("mp4") : suffix;
    return profile;
}

QVector<ExportProfile> ExportProfiles::builtIn()
{
    ExportProfile youTube;
    youTube.id = "youtube";
    youTube.name = tr("YouTube upload");
    youTube.rate = ExportProfile::YouTubeRate;
    youTube.audio = ExportSettings::AacAudio;
    youTube.audioKbps = 384;

    // HEVC holds about the same picture in a good deal less
    ExportProfile small;
    small.id = "small";
    small.name = tr("Small file");
    // AV1 makes the smallest files; HEVC where this ffmpeg has none
    small.codecs = {VideoEncoder::Av1, VideoEncoder::Hevc};
    small.rate = ExportProfile::SourceRate;
    small.sourcePercent = 40;
    small.speed = ExportSettings::Small;
    small.audio = ExportSettings::OpusAudio;
    small.audioKbps = 128;
    small.suffix = "mkv";

    ExportProfile source;
    source.id = "source";
    source.name = tr("Like the video");
    source.rate = ExportProfile::SourceRate;

    // the quickest: the graphics card where there is one, the video's own bitrate
    ExportProfile fast;
    fast.id = "fast";
    fast.name = tr("Fast");
    fast.preferHardware = true;
    fast.rate = ExportProfile::SourceRate;
    fast.speed = ExportSettings::Fast;

    QVector<ExportProfile> list{youTube, small, source, fast};
    for (ExportProfile &profile : list)
        {
            profile.builtIn = true;
        }
    return list;
}

int ExportProfiles::youTubeKbps(const QSize &size, double fps)
{
    // YouTube's recommended upload bitrates for SDR video, standard and high frame rate
    struct Step
    {
        int lines;
        int standard;
        int high;
    };
    static const Step steps[] =
    {
        {2160, 45000, 68000},
        {1440, 16000, 24000},
        {1080, 8000, 12000},
        {720, 5000, 7500},
        {480, 2500, 4000},
        {0, 1000, 1500},
    };
    const int lines = std::min(size.width(), size.height());
    const bool high = fps > 31.0;
    for (const Step &step : steps)
        {
            // a little below the step still counts as it: 2.7K is near 1440p, not 2160p
            if (lines >= step.lines * 0.95)
                {
                    return high ? step.high : step.standard;
                }
        }
    return steps[std::size(steps) - 1].standard;
}

ExportSettings ExportProfiles::apply(const ExportProfile &profile, const ExportSettings &current,
                                     const MediaInfo &info, const QVector<VideoEncoder> &available)
{
    ExportSettings settings = current;
    if (profile.builtIn)
        {
            // the first codec there is an encoder for; the kind of encoder chosen so far (or the graphics
            // card, asked for and there), when it has it
            const bool anyHardware = std::any_of(available.cbegin(), available.cend(),
                                                 [](const VideoEncoder &encoder) { return encoder.hardware; });
            const bool hardware = profile.preferHardware ? anyHardware : VideoEncoders::byId(current.encoder).hardware;
            const VideoEncoder *pick = nullptr;
            for (const VideoEncoder::Codec codec : profile.codecs)
                {
                    for (const VideoEncoder &encoder : available)
                        {
                            if (encoder.codec == codec
                                    && (!pick || (encoder.hardware == hardware && pick->hardware != hardware)))
                                {
                                    pick = &encoder;
                                }
                        }
                    if (pick)
                        {
                            break;
                        }
                }
            if (pick)
                {
                    settings.encoder = pick->id;
                }
            else if (!available.isEmpty())
                {
                    settings.encoder = available.first().id;
                }
        }
    else
        {
            const auto found = std::find_if(available.cbegin(), available.cend(), [&](const VideoEncoder &encoder)
            {
                return encoder.id == profile.encoder;
            });
            if (found != available.cend() || available.isEmpty())
                {
                    settings.encoder = profile.encoder;
                }
        }
    const VideoEncoder encoder = VideoEncoders::byId(settings.encoder);
    settings.rateControl = encoder.hasQuality ? profile.rateControl : ExportSettings::Bitrate;
    settings.quality = profile.builtIn ? encoder.defaultQuality : profile.quality;
    settings.speed = profile.speed;
    settings.height = profile.height;
    settings.audio = profile.audio;
    settings.audioKbps = profile.audioKbps;

    const QSize size = ExportPlan::outputSizeFor(info.size, settings.height);
    switch (profile.rate)
        {
        case ExportProfile::FixedRate:
            settings.bitrateKbps = profile.bitrateKbps;
            break;
        case ExportProfile::YouTubeRate:
            settings.bitrateKbps = youTubeKbps(size, info.fps());
            break;
        case ExportProfile::SourceRate:
        {
            // a video that does not say: what YouTube would take for it
            const int youTube = youTubeKbps(size, info.fps());
            const int source = info.bitrateKbps > 0 ? info.bitrateKbps : youTube;
            int kbps = int(qint64(source) * profile.sourcePercent / 100);
            if (profile.sourcePercent < 100)
                {
                    // a share of a video that is small already would leave nothing of the picture
                    kbps = std::max(kbps, int(qint64(youTube) * profile.sourcePercent / 100));
                }
            settings.bitrateKbps = std::max(500, kbps);
            break;
        }
        }
    settings.outputPath = ExportSettings::withSuffix(current.outputPath, profile.suffix);
    return settings;
}

bool ExportProfiles::sameChoices(const ExportSettings &a, const ExportSettings &b)
{
    const bool quality = a.rateControl == ExportSettings::Quality;
    return a.encoder == b.encoder && a.rateControl == b.rateControl
           && (quality ? a.quality == b.quality : a.bitrateKbps == b.bitrateKbps)
           && a.speed == b.speed && a.height == b.height && a.audio == b.audio
           && a.audioKbps == b.audioKbps
           && QFileInfo(a.outputPath).suffix().compare(QFileInfo(b.outputPath).suffix(), Qt::CaseInsensitive) == 0;
}
