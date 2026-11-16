#include "telemetryimporter.h"

#include "csv/csvimporter.h"
#include "csv/dataprofilelibrary.h"
#include "derivedchannels.h"
#include "fit/fitimporter.h"
#include "gpmfimporter.h"
#include "gpximporter.h"
#include "nmeaimporter.h"
#include "tcximporter.h"
#include "vboimporter.h"

#include <QFileInfo>

#include <algorithm>

namespace
{
// A few samples stamped a day or more away from the rest of the recording (a clock not set yet,
// 1970, a GPS week counted wrong) would stretch the ride over years: everything worked out
// second by second would then run for ages. Such stray bits are left out; parts of a ride
// with a night between them stay, as they hold real data.
TelemetrySessionPtr withoutStrayTimes(const TelemetrySessionPtr &session)
{
    constexpr double kGap = 24.0 * 3600.0;   // apart from the rest by this much at least ...
    constexpr double kShare = 0.01;          // ... and holding no more than this share of it
    if (!session)
        {
            return session;
        }
    const Channel *base = nullptr;
    for (const QString &id : session->channelIds())
        {
            const Channel *channel = session->channel(id);
            if (!base || channel->count() > base->count())
                {
                    base = channel;
                }
        }
    if (!base || base->count() < 2 || base->endTime() - base->startTime() < kGap)
        {
            return session;
        }
    // stretches of the recording without a day-long gap inside
    struct Run
    {
        double start;
        double end;
        int samples;
    };
    QVector<Run> runs{{base->timeAt(0), base->timeAt(0), 1}};
    for (int i = 1; i < base->count(); ++i)
        {
            const double t = base->timeAt(i);
            if (t - runs.last().end >= kGap)
                {
                    runs.append({t, t, 1});
                }
            else
                {
                    runs.last().end = t;
                    ++runs.last().samples;
                }
        }
    QVector<Run> stray;
    for (const Run &run : std::as_const(runs))
        {
            if (run.samples <= std::max(1.0, kShare * base->count()))
                {
                    stray.append(run);
                }
        }
    if (stray.isEmpty() || stray.size() == runs.size())
        {
            return session;
        }
    const QString baseId = base->id();
    auto cleaned = std::make_shared<TelemetrySession>(*session);
    int dropped = 0;
    for (const QString &id : cleaned->channelIds())
        {
            const Channel *channel = cleaned->channel(id);
            Channel &writable = cleaned->channelForWriting(id, channel->unit(), channel->interpolation());
            for (const Run &run : std::as_const(stray))
                {
                    // the samples of other channels near it go too; those of the base are counted
                    const int removed = writable.removeBetween(run.start - kGap / 2.0, run.end + kGap / 2.0);
                    if (id == baseId)
                        {
                            dropped += removed;
                        }
                }
        }
    cleaned->removeEmptyChannels();
    cleaned->addWarning(TelemetryImporter::tr("%n sample(s) stamped a day or more away from the rest of the recording were left out.",
                                              nullptr, dropped));
    return cleaned;
}
}

QStringList TelemetryImporter::suffixes()
{
    // CSV through the data profiles; loggers also write .txt and .log
    // mp4: the positions GoPro cameras write into their videos
    // nmea, nma: GPS sentences; vbo: VBOX loggers
    return {"fit", "gpx", "tcx", "csv", "txt", "log", "mp4", "nmea", "nma", "vbo"};
}

bool TelemetryImporter::canLoad(const QString &path)
{
    return suffixes().contains(QFileInfo(path).suffix().toLower());
}

QString TelemetryImporter::fileDialogFilter()
{
    QStringList patterns;
    for (const QString &suffix : suffixes())
        {
            patterns.append("*." + suffix);
        }
    return tr("Telemetry files (%1);;All files (*)").arg(patterns.join(' '));
}

TelemetrySessionPtr TelemetryImporter::load(const QString &path, QString *error)
{
    return DerivedChannels::complete(loadRecorded(path, error));
}

TelemetrySessionPtr TelemetryImporter::loadRecorded(const QString &path, QString *error)
{
    return withoutStrayTimes(loadFile(path, error));
}

TelemetrySessionPtr TelemetryImporter::loadFile(const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "fit")
        {
            return FitImporter::load(path, error);
        }
    if (suffix == "gpx")
        {
            return GpxImporter::load(path, error);
        }
    if (suffix == "mp4")
        {
            return GpmfImporter::load(path, error);
        }
    if (suffix == "tcx")
        {
            return TcxImporter::load(path, error);
        }
    if (suffix == "nmea" || suffix == "nma" || ((suffix == "txt" || suffix == "log") && NmeaImporter::looksLikeNmea(path)))
        {
            return NmeaImporter::load(path, error);
        }
    if (suffix == "vbo")
        {
            return VboImporter::load(path, error);
        }
    if (suffix == "csv" || suffix == "txt" || suffix == "log")
        {
            return CsvImporter::load(path, DataProfileLibrary::profiles(), error);
        }
    if (error)
        {
            *error = tr("%1: unsupported file type. Supported: %2.").arg(path, suffixes().join(", "));
        }
    return nullptr;
}
