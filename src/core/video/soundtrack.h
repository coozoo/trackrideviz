#pragma once

#include "cutlist.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>

// A piece of music under the video: one file, or a part of it.
struct MusicTrack
{
    QString path;
    double duration{0.0};   // s, of the whole file; 0 = not known (yet)
    double from{0.0};       // where in the file it begins to play
    double to{0.0};         // where it stops; 0 = at its end

    // How long it plays; 0 while the file's length is not known and it plays to the end.
    double length() const;
    // False when nothing of it is left to play.
    bool plays() const { return duration <= 0.0 || length() > 0.0; }

    bool operator==(const MusicTrack &other) const
    {
        return path == other.path && duration == other.duration && from == other.from && to == other.to;
    }
    bool operator!=(const MusicTrack &other) const { return !(*this == other); }
};

// Music played one track after another from a moment of the video on, and
// how loud it and the video's own sound are. The music runs on through what
// is cut out of the video, as it would in the exported file: it follows the
// time of the export, not of the video.
struct Soundtrack
{
    QVector<MusicTrack> tracks;
    double start{0.0};      // video time the first track begins at
    double fade{2.0};       // s: each track fades in at its beginning and out at its end
    int videoVolume{100};   // %
    int musicVolume{100};   // %
    bool videoMuted{false};
    bool musicMuted{false};
    // the music lowered while the camera's sound is loud: talk, an engine
    bool ducking{false};

    // Something of the music is heard: a track with something left to play, not turned down.
    bool hasMusic() const
    {
        return !musicMuted && musicVolume > 0
               && std::any_of(tracks.cbegin(), tracks.cend(), [](const MusicTrack &track) { return track.plays(); });
    }
    static constexpr int kMaxVolume = 200;   // %
    double videoGain() const { return videoMuted ? 0.0 : videoVolume / 100.0; }
    double musicGain() const { return hasMusic() ? musicVolume / 100.0 : 0.0; }
    // Whether the sound of an export differs from the video's own (and cannot be copied).
    bool changesSound() const { return hasMusic() || videoGain() != 1.0; }
    // All tracks one after another; tracks of unknown length count as nothing.
    double length() const;

    // Seconds into the music at video time `videoTime` (negative before it starts).
    double musicTime(double videoTime, const QVector<TimeRange> &cuts) const;

    struct Moment
    {
        int track{-1};          // none: before the music or after it
        double position{0.0};   // s into the track's file
        double gain{0.0};       // 0..1 by the fades, without the volume
    };
    // What plays `musicTime` seconds into the music.
    Moment at(double musicTime) const;

    bool operator==(const Soundtrack &other) const
    {
        return tracks == other.tracks && start == other.start && fade == other.fade && videoVolume == other.videoVolume
               && musicVolume == other.musicVolume && videoMuted == other.videoMuted && musicMuted == other.musicMuted
               && ducking == other.ducking;
    }
    bool operator!=(const Soundtrack &other) const { return !(*this == other); }
};

namespace SoundtrackMix
{
// The filter that makes the music of an export as long as `outputLength`
// seconds: the tracks that play() as inputs `firstInput`, `firstInput` + 1 ... trimmed,
// faded and put end to end, starting `offset` seconds into the export (less
// than 0: that much of the music lies before it), at the music's volume.
// Ends in the label `out`.
QString music(const Soundtrack &soundtrack, int firstInput, double offset, double outputLength, const QString &out);

// The length of an audio file, from ffprobe; 0 when it cannot be read. Blocking.
double probeDuration(const QString &ffprobeProgram, const QString &path);

// What the open dialog offers.
QStringList fileFilters();
}
