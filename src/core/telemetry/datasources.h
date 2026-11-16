#pragma once

#include "telemetrysession.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVector>

// A data file added to the main one: a heart-rate strap's recording next to the GPS, say.
// Its channels are laid onto the main data's time base and taken in its place where both
// have one.
struct DataSource
{
    QString path;              // absolute
    // seconds added to the file's times to put them on the main data's: its clock against
    // the main data's, or the user's value
    double shift{0.0};
    bool shiftIsManual{false};
    QStringList channels;      // the channels taken from it

    bool operator==(const DataSource &other) const
    {
        return path == other.path && shift == other.shift && shiftIsManual == other.shiftIsManual
               && channels == other.channels;
    }
    bool operator!=(const DataSource &other) const { return !(*this == other); }
};

class DataSources
{
    Q_DECLARE_TR_FUNCTIONS(DataSources)

public:
    // The shift that lines the two recordings' clocks up; false when either has no time of day.
    static bool clockShift(const TelemetrySession &main, const TelemetrySession &added, double *shift);
    // What a file is added for unless chosen otherwise: the channels the main data lacks.
    static QStringList missingChannels(const TelemetrySession &main, const TelemetrySession &added);

    // The main data with the channels of the added files, shifted. A file that is missing
    // (nullptr) is a warning of the result; samples before the main data's start are left out.
    static TelemetrySessionPtr merged(const TelemetrySessionPtr &main, const QVector<DataSource> &sources,
                                      const QVector<TelemetrySessionPtr> &added);
    // Loads the added files and merges them; a shift not set by hand follows the clocks.
    // Blocking: for a worker thread.
    static TelemetrySessionPtr loadAndMerge(const TelemetrySessionPtr &main, QVector<DataSource> *sources);
};
