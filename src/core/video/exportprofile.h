#pragma once

#include "exportsettings.h"
#include "mediainfo.h"
#include "videoencoders.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QVector>

// A named set of export choices that fills in the export dialog: the
// built-in ones work out their bitrate from the video, the user's own keep
// what was chosen when they were saved.
struct ExportProfile
{
    enum Rate
    {
        FixedRate,     // bitrateKbps as it is
        YouTubeRate,   // what YouTube recommends for the size and frame rate
        SourceRate     // sourcePercent of the video's own bitrate
    };

    QString id;                 // built-in: "youtube", "small", "source"; the user's own: their name
    QString name;
    bool builtIn{false};
    // built-in: the codecs, the one wanted first; the encoder is one of those found here
    QVector<VideoEncoder::Codec> codecs{VideoEncoder::H264};
    QString encoder;            // the user's own: the exact encoder
    ExportSettings::RateControl rateControl{ExportSettings::Bitrate};
    int quality{23};
    Rate rate{FixedRate};
    int bitrateKbps{20000};
    int sourcePercent{100};
    ExportSettings::Speed speed{ExportSettings::Balanced};
    int height{0};
    ExportSettings::Audio audio{ExportSettings::CopyAudio};
    int audioKbps{192};
    QString suffix{"mp4"};      // the file type

    QJsonObject toJson() const;
    static ExportProfile fromJson(const QJsonObject &json);
    // The user's own profile of these choices.
    static ExportProfile of(const QString &name, const ExportSettings &settings);
};

class ExportProfiles
{
    Q_DECLARE_TR_FUNCTIONS(ExportProfiles)

public:
    static QVector<ExportProfile> builtIn();

    // kbit/s YouTube recommends for an upload of this size (the shorter side, so
    // also for upright video) and frame rate.
    static int youTubeKbps(const QSize &size, double fps);

    // `current` with the choices of `profile` for this video: the encoder
    // among `available` (one like `current.encoder` when it can: graphics card
    // or not), and the file name's suffix changed to the profile's.
    static ExportSettings apply(const ExportProfile &profile, const ExportSettings &current,
                                const MediaInfo &info, const QVector<VideoEncoder> &available);

    // Whether two sets of choices are the same apart from the file and the range.
    static bool sameChoices(const ExportSettings &a, const ExportSettings &b);
};
