#include "dataprofilelibrary.h"

#include "xmldataprofileimporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

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

QString fileFor(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9 ._+-]"), "_");
    safe = safe.simplified();
    while (safe.startsWith('.'))
        {
            safe.remove(0, 1);
        }
    return safe.isEmpty() ? QString() : QDir(DataProfileLibrary::userFolder()).filePath(safe.left(80) + ".json");
}
}

QString DataProfileLibrary::userFolder()
{
    const QMutexLocker lock(&mutex());
    if (!folderOverride().isEmpty())
        {
            return folderOverride();
        }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/dataprofiles";
}

void DataProfileLibrary::setUserFolder(const QString &folder)
{
    const QMutexLocker lock(&mutex());
    folderOverride() = folder;
}

QVector<DataProfile> DataProfileLibrary::profiles()
{
    QVector<DataProfile> result;
    const QDir dir(userFolder());
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        {
            QFile file(dir.filePath(name));
            DataProfile profile;
            if (file.open(QIODevice::ReadOnly) && DataProfileFile::fromJson(file.readAll(), &profile))
                {
                    result.append(profile);
                }
        }
    return result;
}

bool DataProfileLibrary::add(const DataProfile &profile, QString *error)
{
    const QString path = fileFor(profile.name);
    QSaveFile file(path);
    if (path.isEmpty() || !QDir().mkpath(userFolder()) || !file.open(QIODevice::WriteOnly)
            || file.write(DataProfileFile::toJson(profile)) < 0 || !file.commit())
        {
            if (error)
                {
                    *error = tr("Cannot write %1: %2").arg(path, file.errorString());
                }
            return false;
        }
    return true;
}

bool DataProfileLibrary::remove(const QString &name)
{
    const QString path = fileFor(name);
    return !path.isEmpty() && QFile::remove(path);
}

DataProfileLibrary::ImportSummary DataProfileLibrary::importXml(const QStringList &files)
{
    ImportSummary summary;
    for (const QString &file : files)
        {
            const QString fileName = QFileInfo(file).fileName();
            const XmlDataProfileImporter::Result result = XmlDataProfileImporter::importFile(file);
            QString error;
            if (!result.ok)
                {
                    summary.failed << QString("%1: %2").arg(fileName, result.error);
                }
            else if (!add(result.profile, &error))
                {
                    summary.failed << QString("%1: %2").arg(fileName, error);
                }
            else
                {
                    ++summary.imported;
                    if (!result.warnings.isEmpty())
                        {
                            summary.notes << QString("%1: %2").arg(result.profile.name, result.warnings.join("; "));
                        }
                }
        }
    return summary;
}
