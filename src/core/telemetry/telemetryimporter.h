#pragma once

#include "telemetrysession.h"

#include <QCoreApplication>

// Picks the importer for a data file. The one entry point for the GUI, the
// tests and (later) the command line, so a new format is added in one place.
class TelemetryImporter
{
    Q_DECLARE_TR_FUNCTIONS(TelemetryImporter)

public:
    // Lower-case file suffixes without the dot: "fit", "gpx".
    static QStringList suffixes();
    static bool canLoad(const QString &path);
    // Filter string for QFileDialog.
    static QString fileDialogFilter();

    // Returns nullptr on failure and sets *error (if given) to a user-facing message.
    // Blocking; safe to call from a worker thread.
    static TelemetrySessionPtr load(const QString &path, QString *error = nullptr);
};
