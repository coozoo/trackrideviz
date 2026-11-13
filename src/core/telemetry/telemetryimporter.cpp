#include "telemetryimporter.h"

#include "csv/csvimporter.h"
#include "csv/dataprofilelibrary.h"
#include "fit/fitimporter.h"
#include "gpximporter.h"

#include <QFileInfo>

QStringList TelemetryImporter::suffixes()
{
    // CSV through the data profiles; loggers also write .txt and .log
    return {"fit", "gpx", "csv", "txt", "log"};
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
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "fit")
        {
            return FitImporter::load(path, error);
        }
    if (suffix == "gpx")
        {
            return GpxImporter::load(path, error);
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
