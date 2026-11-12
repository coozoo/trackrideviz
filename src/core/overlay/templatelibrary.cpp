#include "templatelibrary.h"

#include "../gauge/gaugelibrary.h"
#include "../gauge/veggaugeimporter.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const char kPrefix[] = "template:";

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

QString safeFileName(const QString &name)
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

QString pathFor(const QString &id)
{
    if (!id.startsWith(kPrefix))
        {
            return QString();
        }
    const QString base = id.mid(int(qstrlen(kPrefix)));
    // an id is text from outside: it must name one file of the library, nothing else
    if (base.isEmpty() || base != safeFileName(base))
        {
            return QString();
        }
    return TemplateLibrary::userFolder() + "/" + base + ".json";
}

QByteArray withoutBom(const QByteArray &json)
{
    return json.startsWith("\xEF\xBB\xBF") ? json.mid(3) : json;
}

QColor colorOf(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isObject())
        {
            return fallback;
        }
    const QJsonObject o = value.toObject();
    return QColor(o.value("r").toInt(), o.value("g").toInt(), o.value("b").toInt(), o.value("a").toInt(255));
}

TemplateItem::Edge edgeOf(const QString &word)
{
    return word == "max" ? TemplateItem::End : word == "mid" ? TemplateItem::Middle : TemplateItem::Start;
}

// Widget entries, with grouped ones flattened: a group gives its anchor to
// its members, whose own origin is a plain offset from it.
struct Placement
{
    QJsonObject widget;
    TemplateItem::Edge edgeX{TemplateItem::Start};
    TemplateItem::Edge edgeY{TemplateItem::Start};
    double offsetX{0.0};
    double offsetY{0.0};
};

QVector<Placement> placementsOf(const QJsonArray &widgets)
{
    QVector<Placement> result;
    for (const QJsonValue &value : widgets)
        {
            const QJsonObject widget = value.toObject();
            const QJsonObject origin = widget.value("origin").toObject();
            const QJsonObject x = origin.value("x").toObject();
            const QJsonObject y = origin.value("y").toObject();
            Placement outer;
            outer.edgeX = edgeOf(x.value("fromEdge").toString());
            outer.edgeY = edgeOf(y.value("fromEdge").toString());
            outer.offsetX = x.value("offset").toDouble();
            outer.offsetY = y.value("offset").toDouble();
            if (widget.contains("widgets"))
                {
                    for (const QJsonValue &memberValue : widget.value("widgets").toArray())
                        {
                            const QJsonObject member = memberValue.toObject();
                            Placement placed = outer;
                            placed.widget = member;
                            placed.offsetX += member.value("origin").toObject().value("x").toDouble();
                            placed.offsetY += member.value("origin").toObject().value("y").toDouble();
                            result.append(placed);
                        }
                    continue;
                }
            outer.widget = widget;
            result.append(outer);
        }
    return result;
}
}

QString TemplateLibrary::userFolder()
{
    const QMutexLocker lock(&mutex());
    if (!folderOverride().isEmpty())
        {
            return folderOverride();
        }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/templates";
}

void TemplateLibrary::setUserFolder(const QString &folder)
{
    const QMutexLocker lock(&mutex());
    folderOverride() = folder;
}

QVector<TemplateLibrary::Entry> TemplateLibrary::entries()
{
    QVector<Entry> result;
    const QDir dir(userFolder());
    for (const QString &file : dir.entryList({"*.json"}, QDir::Files))
        {
            GaugeTemplate t;
            if (TemplateFile::load(dir.filePath(file), &t))
                {
                    result.append({kPrefix + QFileInfo(file).completeBaseName(), t.name, t.category});
                }
        }
    std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b)
    {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

GaugeTemplate TemplateLibrary::get(const QString &id)
{
    GaugeTemplate t;
    const QString path = pathFor(id);
    if (!path.isEmpty())
        {
            TemplateFile::load(path, &t);
        }
    return t;
}

QString TemplateLibrary::add(const GaugeTemplate &gaugeTemplate, QString *error)
{
    const QString base = safeFileName(gaugeTemplate.name);
    if (base.isEmpty())
        {
            if (error)
                {
                    *error = tr("The template has no usable name.");
                }
            return QString();
        }
    if (!QDir().mkpath(userFolder()))
        {
            if (error)
                {
                    *error = tr("Cannot create the folder %1.").arg(userFolder());
                }
            return QString();
        }
    const QString id = kPrefix + base;
    return TemplateFile::save(pathFor(id), gaugeTemplate, error) ? id : QString();
}

bool TemplateLibrary::remove(const QString &id)
{
    const QString path = pathFor(id);
    return !path.isEmpty() && QFile::remove(path);
}

TemplateLibrary::ImportSummary TemplateLibrary::importVet(const QStringList &vetFiles, const QString &widgetsFolder)
{
    ImportSummary summary;
    const QDir widgets(widgetsFolder);
    const QStringList available = widgets.entryList({"*.veg"}, QDir::Files);
    auto findWidget = [&](const QString &fileName)
    {
        for (const QString &candidate : available)
            {
                if (candidate.compare(fileName, Qt::CaseInsensitive) == 0)
                    {
                        return widgets.filePath(candidate);
                    }
            }
        return QString();
    };

    for (const QString &file : vetFiles)
        {
            const QString fileName = QFileInfo(file).fileName();
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly))
                {
                    summary.failed << QString("%1: %2").arg(fileName, input.errorString());
                    continue;
                }
            const QByteArray json = input.readAll();
            // bring in what the template needs and the library lacks
            for (const QString &gaugeFile : VetTemplateImporter::gaugeFiles(json))
                {
                    if (!VetTemplateImporter::findInLibrary(gaugeFile).id.isEmpty())
                        {
                            continue;
                        }
                    const QString source = findWidget(gaugeFile);
                    if (!source.isEmpty() && GaugeLibrary::importVeg({source}).imported == 1)
                        {
                            GaugeLibrary::refresh();
                            ++summary.gaugesImported;
                        }
                }
            const VetTemplateImporter::Result result = VetTemplateImporter::parse(
                        json, VegGaugeImporter::nameFromFileName(QFileInfo(file).completeBaseName()),
                        VetTemplateImporter::findInLibrary);
            if (!result.ok)
                {
                    summary.failed << QString("%1: %2").arg(fileName, result.error);
                    continue;
                }
            QString error;
            if (add(result.gaugeTemplate, &error).isEmpty())
                {
                    summary.failed << QString("%1: %2").arg(fileName, error);
                    continue;
                }
            ++summary.imported;
            if (!result.missingGauges.isEmpty())
                {
                    summary.notes << tr("%1: without %2").arg(result.gaugeTemplate.name, result.missingGauges.join(", "));
                }
        }
    return summary;
}

VetTemplateImporter::FoundGauge VetTemplateImporter::findInLibrary(const QString &fileName)
{
    FoundGauge found;
    found.id = GaugeLibrary::findByName(VegGaugeImporter::nameFromFileName(QFileInfo(fileName).completeBaseName()));
    const Gauge gauge = found.id.isEmpty() ? Gauge() : GaugeLibrary::gauge(found.id);
    if (gauge.isEmpty())
        {
            return FoundGauge();
        }
    found.height = gauge.size.height();
    found.supportedTypes = gauge.supportedTypes;
    return found;
}

GaugeTheme VetTemplateImporter::themeOf(const QColor &background, const QColor &accent, const QColor &accent2)
{
    GaugeTheme theme;
    theme.background = background;
    theme.accent = accent;
    theme.accent2 = accent2;
    // text has to stand out from the plates it is printed on
    const double lightness = (0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue()) / 255.0;
    theme.foreground = lightness > 0.6 && background.alpha() > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
    return theme;
}

QStringList VetTemplateImporter::gaugeFiles(const QByteArray &json)
{
    QStringList files;
    const QJsonObject root = QJsonDocument::fromJson(withoutBom(json)).object();
    for (const Placement &placement : placementsOf(root.value("widgets").toArray()))
        {
            const QString file = placement.widget.value("filename").toString();
            if (!file.isEmpty() && !files.contains(file, Qt::CaseInsensitive))
                {
                    files << file;
                }
        }
    return files;
}

VetTemplateImporter::Result VetTemplateImporter::parse(const QByteArray &json, const QString &name,
        const std::function<FoundGauge(const QString &)> &findGauge)
{
    Result result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(withoutBom(json), &parseError);
    const QJsonObject root = document.object();
    if (!document.isObject() || !root.value("widgets").isArray())
        {
            result.error = tr("This is not a .vet template.");
            return result;
        }
    GaugeTemplate &t = result.gaugeTemplate;
    t.name = root.value("name").toString(name);
    t.category = root.value("category").toString();
    const GaugeTheme defaults;
    t.theme = themeOf(colorOf(root.value("backgroundColor"), defaults.background),
                      colorOf(root.value("accentColor"), defaults.accent),
                      colorOf(root.value("accent2Color"), defaults.accent2));

    for (const Placement &placement : placementsOf(root.value("widgets").toArray()))
        {
            const QString file = placement.widget.value("filename").toString();
            if (file.isEmpty())
                {
                    continue;
                }
            const FoundGauge gauge = findGauge(file);
            if (gauge.id.isEmpty() || gauge.height <= 0.0)
                {
                    if (!result.missingGauges.contains(file))
                        {
                            result.missingGauges << file;
                        }
                    continue;
                }
            TemplateItem item;
            item.gaugeId = gauge.id;
            // without a stated measurement: what the gauge was made for, else speed
            item.channel = VegGaugeImporter::channelFor(placement.widget.value("valueType").toString());
            if (item.channel.isEmpty())
                {
                    item.channel = gauge.supportedTypes.isEmpty() ? QStringLiteral("speed") : gauge.supportedTypes.first();
                }
            item.edgeX = placement.edgeX;
            item.edgeY = placement.edgeY;
            item.offsetX = placement.offsetX;
            item.offsetY = placement.offsetY;
            const double scale = placement.widget.value("scale").toDouble(1.0);
            item.height = gauge.height * (scale > 0.0 ? scale : 1.0);
            t.items.append(item);
        }
    if (t.items.isEmpty())
        {
            result.error = tr("None of the gauges this template uses could be found.");
            return result;
        }
    result.ok = true;
    return result;
}
