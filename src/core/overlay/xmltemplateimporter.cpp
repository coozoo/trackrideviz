#include "xmltemplateimporter.h"

#include "../gauge/gaugelibrary.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace
{
struct Placement
{
    QString name;
    QPointF position;
    double opacity{1.0};
};

// The gauges under <GaugeSettings>, in drawing order; false when this is not a project.
bool readPlacements(const QByteArray &xml, QVector<Placement> *placements, QString *error)
{
    QXmlStreamReader reader(xml);
    bool project = false;
    bool settings = false;
    while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isStartElement())
                {
                    const QStringView name = reader.name();
                    if (!project)
                        {
                            if (name != QLatin1String("Project"))
                                {
                                    break;
                                }
                            project = true;
                        }
                    else if (name == QLatin1String("GaugeSettings"))
                        {
                            settings = true;
                        }
                    else if (settings && name == QLatin1String("Gauge"))
                        {
                            const QXmlStreamAttributes a = reader.attributes();
                            Placement p;
                            p.name = a.value("name").toString();
                            p.position = QPointF(a.value("x").toDouble(), a.value("y").toDouble());
                            const QString opacity = a.value("transparency").toString();
                            p.opacity = opacity.isEmpty() ? 1.0 : opacity.toDouble();
                            if (!p.name.isEmpty())
                                {
                                    placements->append(p);
                                }
                        }
                }
            else if (reader.isEndElement() && reader.name() == QLatin1String("GaugeSettings"))
                {
                    settings = false;
                }
        }
    if (!project || reader.hasError())
        {
            *error = reader.hasError() ? XmlTemplateImporter::tr("This is not an XML template: %1").arg(reader.errorString())
                     : XmlTemplateImporter::tr("This is not an XML template.");
            return false;
        }
    return true;
}

// Where a gauge sits along one side of the frame: the edge it is nearest to,
// and its distance from there in reference pixels.
void place(double start, double length, double frameLength, double scale, TemplateItem::Edge *edge, double *offset)
{
    const double centre = start + length / 2.0;
    if (centre < frameLength / 3.0)
        {
            *edge = TemplateItem::Start;
            *offset = start * scale;
        }
    else if (centre > frameLength * 2.0 / 3.0)
        {
            *edge = TemplateItem::End;
            *offset = (start - frameLength) * scale;
        }
    else
        {
            *edge = TemplateItem::Middle;
            *offset = (start - frameLength / 2.0) * scale;
        }
}
}

QStringList XmlTemplateImporter::gaugeNames(const QByteArray &xml)
{
    QVector<Placement> placements;
    QString error;
    QStringList names;
    if (readPlacements(xml, &placements, &error))
        {
            for (const Placement &p : placements)
                {
                    if (!names.contains(p.name))
                        {
                            names << p.name;
                        }
                }
        }
    return names;
}

XmlTemplateImporter::FoundGauge XmlTemplateImporter::findInLibrary(const QString &gaugeName)
{
    FoundGauge found;
    found.id = GaugeLibrary::findByName(gaugeName);
    const Gauge gauge = found.id.isEmpty() ? Gauge() : GaugeLibrary::gauge(found.id);
    if (gauge.isEmpty())
        {
            return FoundGauge();
        }
    found.size = gauge.size;
    found.supportedTypes = gauge.supportedTypes;
    return found;
}

XmlTemplateImporter::Result XmlTemplateImporter::parse(const QByteArray &xml, const QString &name,
        const std::function<FoundGauge(const QString &)> &findGauge)
{
    Result result;
    QVector<Placement> placements;
    if (!readPlacements(xml, &placements, &result.error))
        {
            return result;
        }

    struct Found
    {
        Placement placement;
        FoundGauge gauge;
    };
    QVector<Found> found;
    QSizeF extent;
    for (const Placement &p : placements)
        {
            const FoundGauge gauge = findGauge(p.name);
            if (gauge.id.isEmpty() || gauge.size.isEmpty())
                {
                    if (!result.missingGauges.contains(p.name))
                        {
                            result.missingGauges << p.name;
                        }
                    continue;
                }
            found.append({p, gauge});
            extent = extent.expandedTo(QSizeF(p.position.x() + gauge.size.width(), p.position.y() + gauge.size.height()));
            if (p.opacity < 1.0)
                {
                    result.notes << tr("%1 is drawn fully opaque").arg(p.name);
                }
        }
    if (found.isEmpty())
        {
            result.error = placements.isEmpty() ? tr("The template places no gauges.")
                           : tr("None of the gauges this template uses could be found.");
            return result;
        }

    // what it was laid out for: the name says, or the smallest common size it fits in
    static const QRegularExpression named("(\\d{3,4})p\\b", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = named.match(name);
    const int namedHeight = match.hasMatch() ? match.captured(1).toInt() : 0;
    if (namedHeight == 1080 || namedHeight == 720)
        {
            result.frameSize = QSize(namedHeight * 16 / 9, namedHeight);
        }
    else
        {
            // gauges may stick out a few pixels
            const QSizeF fits = extent * 0.98;
            for (const QSize &size : {QSize(720, 480), QSize(1280, 720), QSize(1920, 1080)})
                {
                    result.frameSize = size;
                    if (fits.width() <= size.width() && fits.height() <= size.height())
                        {
                            break;
                        }
                }
        }

    GaugeTemplate &t = result.gaugeTemplate;
    t.name = name;
    const double scale = GaugeTemplate::ReferenceHeight / result.frameSize.height();
    for (const Found &f : found)
        {
            TemplateItem item;
            item.gaugeId = f.gauge.id;
            item.channel = f.gauge.supportedTypes.value(0);
            item.height = f.gauge.size.height() * scale;
            place(f.placement.position.x(), f.gauge.size.width(), result.frameSize.width(), scale, &item.edgeX, &item.offsetX);
            place(f.placement.position.y(), f.gauge.size.height(), result.frameSize.height(), scale, &item.edgeY, &item.offsetY);
            t.items.append(item);
        }
    result.ok = true;
    return result;
}
