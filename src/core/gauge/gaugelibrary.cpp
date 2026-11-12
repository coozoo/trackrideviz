#include "gaugelibrary.h"

#include "builtingauges.h"
#include "gaugefile.h"
#include "veggaugeimporter.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
const char kBuiltinPrefix[] = "builtin:";
const char kLibraryPrefix[] = "library:";

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

QHash<QString, Gauge> &cache()
{
    static QHash<QString, Gauge> gauges;
    return gauges;
}

// Folder names come from file names and ids from projects: keep both to
// one harmless path component.
QString safeFolderName(const QString &name)
{
    QString safe = name;
    safe.replace(QRegularExpression("[^A-Za-z0-9 ._-]"), "_");
    safe = safe.simplified();
    while (safe.startsWith('.'))
        {
            safe.remove(0, 1);
        }
    return safe.left(80);
}
}

QString GaugeLibrary::userFolder()
{
    const QMutexLocker lock(&mutex());
    if (!folderOverride().isEmpty())
        {
            return folderOverride();
        }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/gauges";
}

void GaugeLibrary::setUserFolder(const QString &folder)
{
    {
        const QMutexLocker lock(&mutex());
        folderOverride() = folder;
    }
    refresh();
}

void GaugeLibrary::refresh()
{
    const QMutexLocker lock(&mutex());
    cache().clear();
}

QVector<GaugeLibrary::Entry> GaugeLibrary::entries()
{
    QVector<Entry> result;
    for (const QString &id : BuiltinGauges::ids())
        {
            result.append({id, gauge(id).name, true});
        }
    const QDir dir(userFolder());
    const QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString &folder : folders)
        {
            if (QFileInfo::exists(dir.filePath(folder + "/" + GaugeFile::FileName)))
                {
                    const QString id = kLibraryPrefix + folder;
                    const QString name = gauge(id).name;
                    result.append({id, name.isEmpty() ? folder : name, false});
                }
        }
    return result;
}

Gauge GaugeLibrary::gauge(const QString &id)
{
    {
        const QMutexLocker lock(&mutex());
        const auto cached = cache().constFind(id);
        if (cached != cache().constEnd())
            {
                return cached.value();
            }
    }
    Gauge loaded;
    if (id.startsWith(kBuiltinPrefix))
        {
            loaded = BuiltinGauges::byId(id);
        }
    else if (id.startsWith(kLibraryPrefix))
        {
            const QString folder = id.mid(int(qstrlen(kLibraryPrefix)));
            // an id is text from a project file: it must name one folder of the library, nothing else
            if (!folder.isEmpty() && folder == safeFolderName(folder))
                {
                    GaugeFile::load(userFolder() + "/" + folder, &loaded);
                }
        }
    const QMutexLocker lock(&mutex());
    cache().insert(id, loaded);
    return loaded;
}

QString GaugeLibrary::idForName(const QString &name)
{
    const QString folder = safeFolderName(name);
    return folder.isEmpty() ? QString() : kLibraryPrefix + folder;
}

QString GaugeLibrary::findByName(const QString &name)
{
    const QString folder = safeFolderName(name);
    if (folder.isEmpty())
        {
            return QString();
        }
    const QDir dir(userFolder());
    if (QFileInfo::exists(dir.filePath(folder + "/" + GaugeFile::FileName)))
        {
            return kLibraryPrefix + folder;
        }
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (entry.compare(folder, Qt::CaseInsensitive) == 0 && QFileInfo::exists(dir.filePath(entry + "/" + GaugeFile::FileName)))
                {
                    return kLibraryPrefix + entry;
                }
        }
    return QString();
}

QString GaugeLibrary::add(const Gauge &gauge, const QString &folderName, QString *error)
{
    const QString folder = safeFolderName(folderName);
    if (folder.isEmpty())
        {
            if (error)
                {
                    *error = tr("The gauge has no usable name.");
                }
            return QString();
        }
    const QString path = userFolder() + "/" + folder;
    // replace as a whole: pictures of the previous version must not linger
    QDir(path).removeRecursively();
    if (!GaugeFile::save(path, gauge, error))
        {
            return QString();
        }
    const QString id = kLibraryPrefix + folder;
    const QMutexLocker lock(&mutex());
    cache().remove(id);
    return id;
}

bool GaugeLibrary::remove(const QString &id)
{
    if (!id.startsWith(kLibraryPrefix))
        {
            return false;
        }
    const QString folder = id.mid(int(qstrlen(kLibraryPrefix)));
    if (folder.isEmpty() || folder != safeFolderName(folder))
        {
            return false;
        }
    const bool removed = QDir(userFolder() + "/" + folder).removeRecursively();
    const QMutexLocker lock(&mutex());
    cache().remove(id);
    return removed;
}

GaugeLibrary::ImportSummary GaugeLibrary::importVeg(const QStringList &vegFiles)
{
    ImportSummary summary;
    for (const QString &file : vegFiles)
        {
            const QString fileName = QFileInfo(file).fileName();
            const VegGaugeImporter::Result result = VegGaugeImporter::importFile(file);
            if (!result.ok)
                {
                    summary.failed << QString("%1: %2").arg(fileName, result.error);
                    continue;
                }
            QString error;
            if (add(result.gauge, result.gauge.name, &error).isEmpty())
                {
                    summary.failed << QString("%1: %2").arg(fileName, error);
                    continue;
                }
            ++summary.imported;
            if (!result.warnings.isEmpty())
                {
                    ++summary.incomplete;
                    summary.notes << QString("%1: %2").arg(result.gauge.name, result.warnings.join("; "));
                }
        }
    return summary;
}
