#pragma once

#include "channel.h"

#include <QDateTime>
#include <QStringList>
#include <QVector>

#include <map>
#include <memory>

// Everything one data file gave us: channels on a common time base (seconds
// from the first sample), laps recorded by the device, and where it came from.
//
// Importers build a session and hand it out as TelemetrySessionPtr. From then
// on it is immutable, so the GUI, the preview and an export worker can read
// the same object from different threads without locking.
class TelemetrySession
{
public:
    struct Lap
    {
        double startTime{0.0}; // seconds from session start
        double endTime{0.0};
        // where the lap crossed its sector lines, in order; empty without sectors
        QVector<double> splits;
    };

    struct Pause
    {
        double startTime{0.0}; // recording timer stopped (auto-pause, stop button)
        double endTime{0.0};   // ...and started again
    };

    // UTC of time 0. Invalid when the source has no absolute time
    // (then the data can only be synced to video by hand).
    QDateTime startUtc() const { return m_startUtc; }
    void setStartUtc(const QDateTime &startUtc) { m_startUtc = startUtc; }

    // Local time minus UTC, in seconds, when the source states it. Needed to
    // match recordings against sources that only know local time, such as
    // camera file names or exports that write local time labelled as UTC.
    bool hasUtcOffset() const { return m_hasUtcOffset; }
    int utcOffsetSeconds() const { return m_utcOffsetSeconds; }
    void setUtcOffsetSeconds(int seconds) { m_utcOffsetSeconds = seconds; m_hasUtcOffset = true; }

    // End time of the longest channel.
    double duration() const;

    // Insertion order, i.e. the order the importer met them.
    QStringList channelIds() const { return m_order; }
    bool hasChannel(const QString &id) const { return m_channels.find(id) != m_channels.end(); }
    // nullptr when there is no such channel.
    const Channel *channel(const QString &id) const;

    // Returns the channel with this id, creating it on first use. The
    // reference stays valid while other channels are created.
    Channel &channelForWriting(const QString &id, const QString &unit,
                               Channel::Interpolation interpolation = Channel::Linear);
    // Channels that never received a sample are noise in the UI.
    void removeEmptyChannels();
    void removeChannel(const QString &id);

    const QVector<Lap> &laps() const { return m_laps; }
    void addLap(const Lap &lap) { m_laps.append(lap); }
    void setLaps(const QVector<Lap> &laps) { m_laps = laps; }

    // Stretches where the device was not recording. Channels have no samples
    // there; values in between must not be read as real movement.
    const QVector<Pause> &pauses() const { return m_pauses; }
    void addPause(const Pause &pause) { m_pauses.append(pause); }

    // Free-form description of the source: format, device, creator.
    QString sourceInfo() const { return m_sourceInfo; }
    void setSourceInfo(const QString &info) { m_sourceInfo = info; }

    // Channels worked out from others (DerivedChannels), not recorded.
    QStringList derivedChannels() const { return m_derived; }
    void addDerivedChannel(const QString &id) { m_derived.append(id); }

    // Non-fatal problems met while importing (bad CRC, truncated file...).
    QStringList warnings() const { return m_warnings; }
    void addWarning(const QString &warning) { m_warnings.append(warning); }

private:
    QDateTime m_startUtc;
    // node based on purpose: importers hold references to several channels while adding more
    std::map<QString, Channel> m_channels;
    QStringList m_order;
    QVector<Lap> m_laps;
    QVector<Pause> m_pauses;
    bool m_hasUtcOffset{false};
    int m_utcOffsetSeconds{0};
    QString m_sourceInfo;
    QStringList m_derived;
    QStringList m_warnings;
};

using TelemetrySessionPtr = std::shared_ptr<const TelemetrySession>;
