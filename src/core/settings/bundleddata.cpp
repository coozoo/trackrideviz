#include "bundleddata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>

namespace
{
QMutex &mutex()
{
    static QMutex m;
    return m;
}

QString &folderOverride()
{
    static QString folder;
    return folder;
}

bool holdsData(const QString &folder)
{
    return !folder.isEmpty()
           && (QFileInfo(folder + "/gauges").isDir() || QFileInfo(folder + "/templates").isDir());
}

QString search()
{
    QStringList candidates;
    candidates << qEnvironmentVariable("TRACKRIDEVIZ_DATA");
    const QString programFolder = QCoreApplication::applicationDirPath();
    if (!programFolder.isEmpty())
        {
            candidates << programFolder + "/data"
                       << programFolder + "/../share/trackrideviz"
                       << programFolder + "/../Resources";
        }
    candidates << QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, "trackrideviz",
                                            QStandardPaths::LocateDirectory);
#ifdef RTV_SOURCE_DATA
    candidates << QStringLiteral(RTV_SOURCE_DATA);
#endif
    for (const QString &candidate : candidates)
        {
            if (holdsData(candidate))
                {
                    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
                }
        }
    return QString();
}
}

QString BundledData::folder()
{
    const QMutexLocker lock(&mutex());
    if (!folderOverride().isEmpty())
        {
            return folderOverride();
        }
    // the program does not move while it runs
    static const QString found = search();
    return found;
}

QString BundledData::gaugesFolder()
{
    const QString base = folder();
    return base.isEmpty() ? QString() : base + "/gauges";
}

QString BundledData::templatesFolder()
{
    const QString base = folder();
    return base.isEmpty() ? QString() : base + "/templates";
}

void BundledData::setFolder(const QString &folder)
{
    const QMutexLocker lock(&mutex());
    folderOverride() = folder;
}
