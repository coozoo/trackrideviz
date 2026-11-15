#include "gaugetemplate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const char kFormat[] = "rtvtemplate";
const int kVersion = 1;
const char *const kEdges[] = {"start", "middle", "end"};

TemplateItem::Edge edgeOf(const QString &word)
{
    for (int i = 0; i < 3; ++i)
        {
            if (word == QLatin1String(kEdges[i]))
                {
                    return TemplateItem::Edge(i);
                }
        }
    return TemplateItem::Start;
}

double edgePosition(TemplateItem::Edge edge, double length)
{
    return edge == TemplateItem::Start ? 0.0 : edge == TemplateItem::Middle ? length / 2.0 : length;
}
}

QVector<OverlayItem> GaugeTemplate::overlayItems(double frameAspect) const
{
    // the frame in reference pixels: the height stays, the width follows the shape
    const double height = ReferenceHeight;
    const double width = height * (frameAspect > 0.0 ? frameAspect : ReferenceWidth / ReferenceHeight);
    QVector<OverlayItem> result;
    for (const TemplateItem &item : items)
        {
            OverlayItem placed;
            placed.gaugeId = item.gaugeId;
            placed.channel = item.channel;
            placed.x = (edgePosition(item.edgeX, width) + item.offsetX) / width;
            placed.y = (edgePosition(item.edgeY, height) + item.offsetY) / height;
            placed.height = item.height / height;
            placed.streets = item.streets;
            placed.closeUp = item.closeUp;
            placed.headingUp = item.headingUp;
            placed.units = item.units;
            placed.channels = item.channels;
            placed.scaleMax = item.scaleMax;
            result.append(placed);
        }
    return result;
}

GaugeTemplate GaugeTemplate::fromOverlay(const Overlay &overlay, const GaugeTheme &theme, double frameAspect)
{
    const double height = ReferenceHeight;
    const double width = height * (frameAspect > 0.0 ? frameAspect : ReferenceWidth / ReferenceHeight);
    const QRectF frame(0.0, 0.0, width, height);
    auto nearest = [](double middle, double length)
    {
        return middle < length / 3.0 ? TemplateItem::Start : middle > length * 2.0 / 3.0 ? TemplateItem::End : TemplateItem::Middle;
    };
    GaugeTemplate result;
    result.theme = theme;
    for (int i = 0; i < overlay.count(); ++i)
        {
            const OverlayItem &placed = overlay.item(i);
            const QRectF rect = overlay.itemRect(i, frame);
            TemplateItem item;
            item.gaugeId = placed.gaugeId;
            item.channel = placed.channel;
            item.edgeX = nearest(rect.center().x(), width);
            item.edgeY = nearest(rect.center().y(), height);
            item.offsetX = rect.left() - edgePosition(item.edgeX, width);
            item.offsetY = rect.top() - edgePosition(item.edgeY, height);
            item.height = rect.height();
            item.streets = placed.streets;
            item.closeUp = placed.closeUp;
            item.headingUp = placed.headingUp;
            item.units = placed.units;
            item.channels = placed.channels;
            item.scaleMax = placed.scaleMax;
            result.items.append(item);
        }
    return result;
}

QByteArray TemplateFile::toJson(const GaugeTemplate &t)
{
    QJsonArray items;
    for (const TemplateItem &item : t.items)
        {
            QJsonObject object
            {
                {"gauge", item.gaugeId},
                {"channel", item.channel},
                {"edgeX", kEdges[item.edgeX]},
                {"edgeY", kEdges[item.edgeY]},
                {"offsetX", item.offsetX},
                {"offsetY", item.offsetY},
                {"height", item.height},
            };
            if (item.streets != OverlayItem::AsDesigned)
                {
                    object.insert("streets", item.streets == OverlayItem::Shown);
                }
            if (item.closeUp >= 0.0)
                {
                    object.insert("closeUp", item.closeUp);
                }
            if (item.headingUp >= 0)
                {
                    object.insert("headingUp", item.headingUp > 0);
                }
            if (item.units != OverlayItem::AsSettings)
                {
                    object.insert("units", item.units == OverlayItem::ImperialUnits ? "imperial" : "metric");
                }
            OverlayItem choices;
            choices.channels = item.channels;
            choices.scaleMax = item.scaleMax;
            choices.writeChoices(object);
            items.append(object);
        }
    const QJsonObject root
    {
        {"format", kFormat},
        {"version", kVersion},
        {"name", t.name},
        {"category", t.category},
        {"theme", t.theme.toJson()},
        {"items", items},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool TemplateFile::fromJson(const QByteArray &json, GaugeTemplate *t, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject())
        {
            return fail(tr("This is not a template file: %1").arg(parseError.errorString()));
        }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != QLatin1String(kFormat))
        {
            return fail(tr("This is not a Race Track Visualizer template file."));
        }
    if (root.value("version").toInt() > kVersion)
        {
            return fail(tr("The template was saved by a newer version of the program (file version %1).")
                        .arg(root.value("version").toInt()));
        }
    GaugeTemplate loaded;
    loaded.name = root.value("name").toString();
    loaded.category = root.value("category").toString();
    loaded.theme = GaugeTheme::fromJson(root.value("theme").toObject());
    for (const QJsonValue &value : root.value("items").toArray())
        {
            const QJsonObject object = value.toObject();
            TemplateItem item;
            item.gaugeId = object.value("gauge").toString();
            item.channel = object.value("channel").toString();
            item.edgeX = edgeOf(object.value("edgeX").toString());
            item.edgeY = edgeOf(object.value("edgeY").toString());
            item.offsetX = object.value("offsetX").toDouble();
            item.offsetY = object.value("offsetY").toDouble();
            item.height = object.value("height").toDouble();
            if (object.contains("streets"))
                {
                    item.streets = object.value("streets").toBool() ? OverlayItem::Shown : OverlayItem::Hidden;
                }
            item.closeUp = object.value("closeUp").toDouble(-1.0);
            if (object.contains("headingUp"))
                {
                    item.headingUp = object.value("headingUp").toBool() ? 1 : 0;
                }
            if (object.contains("units"))
                {
                    item.units = object.value("units").toString() == QLatin1String("imperial") ? OverlayItem::ImperialUnits
                                 : OverlayItem::MetricUnits;
                }
            OverlayItem choices;
            choices.readChoices(object);
            item.channels = choices.channels;
            item.scaleMax = choices.scaleMax;
            if (!item.gaugeId.isEmpty() && item.height > 0.0)
                {
                    loaded.items.append(item);
                }
        }
    *t = loaded;
    return true;
}

bool TemplateFile::save(const QString &path, const GaugeTemplate &t, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson(t)) < 0 || !file.commit())
        {
            if (error)
                {
                    *error = tr("Cannot write %1: %2").arg(path, file.errorString());
                }
            return false;
        }
    return true;
}

bool TemplateFile::load(const QString &path, GaugeTemplate *t, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = tr("Cannot open %1: %2").arg(path, file.errorString());
                }
            return false;
        }
    return fromJson(file.readAll(), t, error);
}
