#pragma once

#include "../telemetrysession.h"
#include "dataprofile.h"

#include <QCoreApplication>

// Reads a CSV file the way a data profile describes it.
//
// Cells may hold numbers (with a decimal comma when the profile says so),
// clock times and dates (counted in seconds from the first one in their
// column), minutes:seconds, and hemisphere letters (N and E are 1, S and W
// -1; "12.5S" is -12.5). Values come in the unit the mapping names and are
// stored the way channels store them (channelids.h). The recording's start
// is taken from the time-of-day and date columns, as UTC.
class CsvImporter
{
    Q_DECLARE_TR_FUNCTIONS(CsvImporter)

public:
    // The first of `profiles` that recognises a file starting with `lines`; -1 when none.
    static int findProfile(const QStringList &lines, const QVector<DataProfile> &profiles);

    // Reads the file with the first profile that recognises it. nullptr on
    // failure, with *error set to a user-facing message.
    static TelemetrySessionPtr load(const QString &path, const QVector<DataProfile> &profiles, QString *error = nullptr);
    static TelemetrySessionPtr parse(const QByteArray &data, const DataProfile &profile, QString *error = nullptr);

    // A file's text as lines, whatever its encoding and line ends.
    static QStringList lines(const QByteArray &data);
};
