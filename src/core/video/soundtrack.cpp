#include "soundtrack.h"

#include "ffmpegtools.h"

#include <algorithm>
#include <cmath>

namespace
{
QString seconds(double value)
{
    return QString::number(value, 'f', 3);
}
}

double MusicTrack::length() const
{
    const double end = to > 0.0 ? (duration > 0.0 ? std::min(to, duration) : to) : duration;
    return std::max(0.0, end - from);
}

double Soundtrack::length() const
{
    double total = 0.0;
    for (const MusicTrack &track : tracks)
        {
            total += track.length();
        }
    return total;
}

double Soundtrack::musicTime(double videoTime, const QVector<TimeRange> &cuts) const
{
    // time of the export: what is cut out does not count
    auto exportTime = [&cuts](double t)
    {
        return t <= 0.0 ? t : CutList::keptLength(CutList::kept(cuts, 0.0, t));
    };
    return exportTime(videoTime) - exportTime(start);
}

Soundtrack::Moment Soundtrack::at(double musicTime) const
{
    Moment moment;
    if (musicTime < 0.0)
        {
            return moment;
        }
    double begin = 0.0;
    for (int i = 0; i < tracks.size(); ++i)
        {
            const MusicTrack &track = tracks.at(i);
            const double length = track.length();
            const double into = musicTime - begin;
            // a track whose length is not known yet plays to its end, which only the player finds
            if (length <= 0.0 && track.duration <= 0.0)
                {
                    moment.track = i;
                    moment.position = track.from + into;
                    moment.gain = fade > 0.0 ? std::min(1.0, into / fade) : 1.0;
                    return moment;
                }
            if (into < length)
                {
                    moment.track = i;
                    moment.position = track.from + into;
                    // at most half the track for each fade
                    const double f = std::min(fade, length / 2.0);
                    moment.gain = f > 0.0 ? std::clamp(std::min(into, length - into) / f, 0.0, 1.0) : 1.0;
                    return moment;
                }
            begin += length;
        }
    return moment;
}

QString SoundtrackMix::music(const Soundtrack &soundtrack, int firstInput, double offset, double outputLength, const QString &out)
{
    QStringList parts;
    QString joined;
    int count = 0;
    for (int i = 0; i < soundtrack.tracks.size(); ++i)
        {
            const MusicTrack &track = soundtrack.tracks.at(i);
            const double length = track.length();
            if (!track.plays())
                {
                    continue;
                }
            // all alike, so they can be put end to end
            QStringList chain;
            QString trim = QString("atrim=start=%1").arg(seconds(track.from));
            if (length > 0.0)
                {
                    trim += QString(":end=%1").arg(seconds(track.from + length));
                }
            chain << trim << "asetpts=PTS-STARTPTS" << "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo";
            const double fade = length > 0.0 ? std::min(soundtrack.fade, length / 2.0) : soundtrack.fade;
            if (fade > 0.0)
                {
                    chain << QString("afade=t=in:d=%1").arg(seconds(fade));
                    if (length > 0.0)
                        {
                            chain << QString("afade=t=out:st=%1:d=%2").arg(seconds(length - fade), seconds(fade));
                        }
                }
            parts << QString("[%1:a]%2[m%3]").arg(firstInput + count).arg(chain.join(',')).arg(count);
            joined += QString("[m%1]").arg(count);
            ++count;
        }
    if (count == 0)
        {
            return QString();
        }
    QStringList chain;
    if (count > 1)
        {
            chain << QString("concat=n=%1:v=0:a=1").arg(count);
        }
    else
        {
            chain << "anull";
        }
    if (offset > 0.0)
        {
            chain << QString("adelay=delays=%1:all=1").arg(qint64(std::llround(offset * 1000.0)));
        }
    else if (offset < 0.0)
        {
            chain << QString("atrim=start=%1").arg(seconds(-offset)) << "asetpts=PTS-STARTPTS";
        }
    chain << QString("volume=%1").arg(soundtrack.musicGain(), 0, 'f', 3)
          << QString("atrim=end=%1").arg(seconds(outputLength));
    parts << QString("%1%2%3").arg(joined, chain.join(','), out);
    return parts.join(';');
}

double SoundtrackMix::probeDuration(const QString &ffprobeProgram, const QString &path)
{
    const FfmpegTools::RunResult result = FfmpegTools::run(ffprobeProgram,
    {
        "-v", "error", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", path
    });
    if (!result.started || result.exitCode != 0)
        {
            return 0.0;
        }
    bool ok = false;
    const double duration = QString::fromLatin1(result.output).trimmed().toDouble(&ok);
    return ok && std::isfinite(duration) && duration > 0.0 ? duration : 0.0;
}

QStringList SoundtrackMix::fileFilters()
{
    return {QCoreApplication::translate("SoundtrackMix", "Music (*.mp3 *.ogg *.oga *.opus *.flac *.wav *.m4a *.aac *.wma)"),
            QCoreApplication::translate("SoundtrackMix", "All files (*)")};
}
