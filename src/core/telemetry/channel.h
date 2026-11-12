#pragma once

#include <QString>
#include <QVector>

// One telemetry signal (speed, heart rate, latitude...) with its own time base.
// Channels keep the sample rate of their source: GPS at 1-25 Hz, HR at 1 Hz,
// accelerometer at 200 Hz. There is no common grid.
//
// Samples are appended in increasing time order while loading and the channel
// is read-only afterwards.
class Channel
{
public:
    enum Interpolation
    {
        Linear, // speed, altitude, position
        Step    // lap number, gear: hold the previous sample
    };

    Channel() = default;
    Channel(const QString &id, const QString &unit, Interpolation interpolation = Linear);

    QString id() const { return m_id; }
    QString unit() const { return m_unit; }
    Interpolation interpolation() const { return m_interpolation; }

    int count() const { return m_times.size(); }
    bool isEmpty() const { return m_times.isEmpty(); }
    double startTime() const;
    double endTime() const;
    // Smallest and largest sample; 0 for an empty channel. Scans all samples.
    double minValue() const;
    double maxValue() const;

    // timeSec is seconds from the session start. Samples that are not later
    // than the previous one are dropped (returns false) - loggers do repeat
    // and reorder records.
    bool append(double timeSec, double value);

    // Value at timeSec. Outside the recorded range, and for an empty channel,
    // *ok is set to false and the nearest sample (or 0) is returned.
    double valueAt(double timeSec, bool *ok = nullptr) const;

private:
    QString m_id;
    QString m_unit;
    Interpolation m_interpolation{Linear};
    QVector<double> m_times;
    QVector<double> m_values;
};
