#include "gaugelibrary.h"

#include "../settings/bundleddata.h"
#include "builtingauges.h"
#include "gaugefile.h"
#include "veggaugeimporter.h"
#include "xmlgaugeimporter.h"
#include "../telemetry/channelids.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
const char kBuiltinPrefix[] = "builtin:";
const char kBundledPrefix[] = "bundled:";
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

// The gauge folders directly under `folder`, by name.
QStringList gaugeFolders(const QString &folder)
{
    QStringList result;
    if (folder.isEmpty())
        {
            return result;
        }
    const QDir dir(folder);
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
        {
            if (QFileInfo::exists(dir.filePath(entry + "/" + GaugeFile::FileName)))
                {
                    result << entry;
                }
        }
    return result;
}

// The folder an id with this prefix names under `base`; empty when it names none.
QString folderOf(const QString &id, const char *prefix, const QString &base)
{
    if (!id.startsWith(prefix) || base.isEmpty())
        {
            return QString();
        }
    const QString folder = id.mid(int(qstrlen(prefix)));
    // an id is text from a project file: it must name one folder of the library, nothing else
    return !folder.isEmpty() && folder == safeFolderName(folder) ? base + "/" + folder : QString();
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
    auto addFolders = [&](const QString &base, const char *prefix, bool builtin)
    {
        for (const QString &folder : gaugeFolders(base))
            {
                const QString id = prefix + folder;
                const QString name = gauge(id).name;
                result.append({id, name.isEmpty() ? folder : name, builtin});
            }
    };
    addFolders(BundledData::gaugesFolder(), kBundledPrefix, true);
    addFolders(userFolder(), kLibraryPrefix, false);
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
    else
        {
            QString folder = folderOf(id, kLibraryPrefix, userFolder());
            if (folder.isEmpty())
                {
                    folder = folderOf(id, kBundledPrefix, BundledData::gaugesFolder());
                }
            if (!folder.isEmpty())
                {
                    GaugeFile::load(folder, &loaded);
                    VegGaugeImporter::mapChannels(&loaded);
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
    // what came with the program stays
    const QString folder = folderOf(id, kLibraryPrefix, userFolder());
    if (folder.isEmpty())
        {
            return false;
        }
    const bool removed = QDir(folder).removeRecursively();
    const QMutexLocker lock(&mutex());
    cache().remove(id);
    return removed;
}

namespace
{
// Stores what an importer made of one file and counts it.
template <typename Result>
void store(const QString &source, const Result &result, GaugeLibrary::ImportSummary *summary)
{
    if (!result.ok)
        {
            summary->failed << QString("%1: %2").arg(source, result.error);
            return;
        }
    QString error;
    if (GaugeLibrary::add(result.gauge, result.gauge.name, &error).isEmpty())
        {
            summary->failed << QString("%1: %2").arg(source, error);
            return;
        }
    ++summary->imported;
    if (!result.warnings.isEmpty())
        {
            ++summary->incomplete;
            summary->notes << QString("%1: %2").arg(result.gauge.name, result.warnings.join("; "));
        }
}
}

GaugeLibrary::ImportSummary GaugeLibrary::importVeg(const QStringList &vegFiles)
{
    ImportSummary summary;
    for (const QString &file : vegFiles)
        {
            store(QFileInfo(file).fileName(), VegGaugeImporter::importFile(file), &summary);
        }
    return summary;
}

GaugeLibrary::ImportSummary GaugeLibrary::importXml(const QStringList &gaugeFolders)
{
    ImportSummary summary;
    for (const QString &folder : gaugeFolders)
        {
            store(QFileInfo(folder).fileName(), XmlGaugeImporter::importFolder(folder), &summary);
        }
    return summary;
}

QString GaugeLibrary::defaultChannel(const Gauge &gauge, const QStringList &available)
{
    QStringList made;
    for (const QString &type : gauge.supportedTypes)
        {
            // gauges imported earlier keep the other program's names; "default..." stands for anything
            const QString channel = VegGaugeImporter::channelFor(type);
            if (!channel.isEmpty() && !channel.startsWith(QLatin1String("default")) && !made.contains(channel))
                {
                    made << channel;
                }
        }
    for (const QString &channel : made)
        {
            if (available.contains(channel))
                {
                    return channel;
                }
        }
    if (!made.isEmpty())
        {
            return made.first();
        }
    return available.contains(ChannelId::Speed) || available.isEmpty() ? QString(ChannelId::Speed) : available.first();
}
