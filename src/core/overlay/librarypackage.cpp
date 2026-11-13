#include "librarypackage.h"

#include "../gauge/gaugefile.h"
#include "../gauge/gaugelibrary.h"
#include "../util/gzip.h"
#include "templatelibrary.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace
{
const char kGaugeFormat[] = "rtvgauge-package";
const char kTemplateFormat[] = "rtvtemplate-package";
const int kVersion = 1;
const char kLibraryPrefix[] = "library:";

bool fail(QString *error, const QString &message)
{
    if (error)
        {
            *error = message;
        }
    return false;
}

// The files of the gauge's folder, as GaugeFile would write it.
bool gaugeFiles(const Gauge &gauge, QJsonObject *files, QString *error)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        {
            return fail(error, LibraryPackage::tr("A temporary folder could not be made."));
        }
    if (!GaugeFile::save(dir.path(), gauge, error))
        {
            return false;
        }
    QDirIterator it(dir.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        {
            const QString path = it.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                {
                    return fail(error, LibraryPackage::tr("Cannot read %1.").arg(path));
                }
            const QByteArray content = file.readAll();
            const QString relative = QDir(dir.path()).relativeFilePath(path);
            // what is text stays readable in the package
            const bool text = relative.endsWith(".json", Qt::CaseInsensitive) || relative.endsWith(".svg", Qt::CaseInsensitive);
            files->insert(relative, text ? QJsonObject{{"text", QString::fromUtf8(content)}}
                          : QJsonObject{{"base64", QString::fromLatin1(content.toBase64())}});
        }
    return true;
}

bool safeRelative(const QString &path)
{
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\')
           && !path.split('/').contains(QStringLiteral("..")) && !path.contains(':');
}

bool gaugeFromFiles(const QJsonObject &files, Gauge *gauge, QString *error)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        {
            return fail(error, LibraryPackage::tr("A temporary folder could not be made."));
        }
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        {
            if (!safeRelative(it.key()))
                {
                    return fail(error, LibraryPackage::tr("The package names a file outside of it: %1").arg(it.key()));
                }
            const QJsonObject entry = it.value().toObject();
            const QByteArray content = entry.contains("text") ? entry.value("text").toString().toUtf8()
                                       : QByteArray::fromBase64(entry.value("base64").toString().toLatin1());
            const QString path = dir.filePath(it.key());
            QFile file(path);
            if (!QDir().mkpath(QFileInfo(path).path()) || !file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
                {
                    return fail(error, LibraryPackage::tr("Cannot write %1.").arg(path));
                }
        }
    return GaugeFile::load(dir.path(), gauge, error);
}

QByteArray write(const QJsonObject &root)
{
    return Gzip::compress(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool read(const QByteArray &package, const char *format, QJsonObject *root, QString *error)
{
    bool ok = true;
    const QByteArray json = Gzip::isGzip(package) ? Gzip::decompress(package, &ok) : package;
    if (!ok)
        {
            return fail(error, LibraryPackage::tr("The file is damaged."));
        }
    *root = QJsonDocument::fromJson(json).object();
    if (root->value("format").toString() != QLatin1String(format))
        {
            return fail(error, LibraryPackage::tr("This is not a file of this kind."));
        }
    if (root->value("version").toInt() > kVersion)
        {
            return fail(error, LibraryPackage::tr("The file was made by a newer version of the program."));
        }
    return true;
}

// Into the library, unless it is there already as it is; a name of its own when one of that name differs.
QString addGauge(Gauge gauge, const QJsonObject &files, bool *added, QString *error)
{
    *added = false;
    const QString base = gauge.name;
    for (int n = 1; n < 100; ++n)
        {
            const QString name = n == 1 ? base : QString("%1 (%2)").arg(base).arg(n);
            const QString existing = GaugeLibrary::findByName(name);
            if (existing.isEmpty())
                {
                    gauge.name = name;
                    *added = true;
                    return GaugeLibrary::add(gauge, name, error);
                }
            QJsonObject existingFiles;
            if (gaugeFiles(GaugeLibrary::gauge(existing), &existingFiles, nullptr) && existingFiles == files)
                {
                    return existing;
                }
        }
    fail(error, LibraryPackage::tr("There are too many gauges named %1.").arg(base));
    return QString();
}
}

QByteArray LibraryPackage::packGauge(const Gauge &gauge, QString *error)
{
    QJsonObject files;
    if (!gaugeFiles(gauge, &files, error))
        {
            return QByteArray();
        }
    return write(QJsonObject{{"format", kGaugeFormat}, {"version", kVersion}, {"files", files}});
}

bool LibraryPackage::unpackGauge(const QByteArray &package, Gauge *gauge, QString *error)
{
    QJsonObject root;
    return read(package, kGaugeFormat, &root, error) && gaugeFromFiles(root.value("files").toObject(), gauge, error);
}

QByteArray LibraryPackage::packTemplate(const GaugeTemplate &gaugeTemplate, QString *error)
{
    QJsonObject gauges;
    for (const TemplateItem &item : gaugeTemplate.items)
        {
            // what comes with the program is there wherever the template goes
            if (!item.gaugeId.startsWith(QLatin1String(kLibraryPrefix)) || gauges.contains(item.gaugeId))
                {
                    continue;
                }
            const Gauge gauge = GaugeLibrary::gauge(item.gaugeId);
            QJsonObject files;
            if (gauge.isEmpty())
                {
                    fail(error, tr("The template uses a gauge that is not in the library: %1").arg(item.gaugeId));
                    return QByteArray();
                }
            if (!gaugeFiles(gauge, &files, error))
                {
                    return QByteArray();
                }
            gauges.insert(item.gaugeId, files);
        }
    const QJsonObject templateJson = QJsonDocument::fromJson(TemplateFile::toJson(gaugeTemplate)).object();
    return write(QJsonObject{{"format", kTemplateFormat}, {"version", kVersion}, {"template", templateJson},
        {"gauges", gauges}});
}

QString LibraryPackage::importGauge(const QByteArray &package, QString *error)
{
    QJsonObject root;
    Gauge gauge;
    if (!read(package, kGaugeFormat, &root, error) || !gaugeFromFiles(root.value("files").toObject(), &gauge, error))
        {
            return QString();
        }
    bool added = false;
    return addGauge(gauge, root.value("files").toObject(), &added, error);
}

QString LibraryPackage::importTemplate(const QByteArray &package, int *gaugesAdded, QString *error)
{
    if (gaugesAdded)
        {
            *gaugesAdded = 0;
        }
    QJsonObject root;
    if (!read(package, kTemplateFormat, &root, error))
        {
            return QString();
        }
    GaugeTemplate gaugeTemplate;
    if (!TemplateFile::fromJson(QJsonDocument(root.value("template").toObject()).toJson(), &gaugeTemplate, error))
        {
            return QString();
        }
    // the gauges first, so the template can name them by their ids here
    QHash<QString, QString> ids;
    const QJsonObject gauges = root.value("gauges").toObject();
    for (auto it = gauges.constBegin(); it != gauges.constEnd(); ++it)
        {
            Gauge gauge;
            const QJsonObject files = it.value().toObject();
            if (!gaugeFromFiles(files, &gauge, error))
                {
                    return QString();
                }
            bool added = false;
            const QString id = addGauge(gauge, files, &added, error);
            if (id.isEmpty())
                {
                    return QString();
                }
            ids.insert(it.key(), id);
            if (added && gaugesAdded)
                {
                    ++*gaugesAdded;
                }
        }
    for (TemplateItem &item : gaugeTemplate.items)
        {
            item.gaugeId = ids.value(item.gaugeId, item.gaugeId);
        }
    // one of the same name is not replaced
    QStringList names;
    for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
        {
            names << entry.name.toLower();
        }
    const QString base = gaugeTemplate.name;
    for (int n = 2; names.contains(gaugeTemplate.name.toLower()) && n < 100; ++n)
        {
            gaugeTemplate.name = QString("%1 (%2)").arg(base).arg(n);
        }
    return TemplateLibrary::add(gaugeTemplate, error);
}
