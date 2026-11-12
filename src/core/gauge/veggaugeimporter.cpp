#include "veggaugeimporter.h"

#include "../telemetry/channelids.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace
{
struct Context
{
    QString imageFolder;
    QMap<QString, int> skipped;     // component or manipulation type -> how many
    QStringList missingImages;
};

double number(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toVariant().toDouble();
}

// Flags are written as true/false in some files and as 1/0 in others.
bool flag(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toVariant().toBool();
}

QRectF rectOf(const QJsonObject &object)
{
    return QRectF(number(object, "x"), number(object, "y"), number(object, "width"), number(object, "height"));
}

QColor colorOf(const QJsonObject &object)
{
    return QColor(int(number(object, "r")), int(number(object, "g")), int(number(object, "b")),
                  object.contains("a") ? int(number(object, "a")) : 255);
}

GaugeColor colorStyle(const QJsonValue &value)
{
    GaugeColor color;
    if (value.isObject())
        {
            color.role = GaugeColor::Custom;
            color.custom = colorOf(value.toObject());
            return color;
        }
    const QString style = value.toString();
    if (style == "autoForeground")
        {
            color.role = GaugeColor::Foreground;
        }
    else if (style == "background")
        {
            color.role = GaugeColor::Background;
        }
    else if (style == "accent")
        {
            color.role = GaugeColor::Accent;
        }
    else if (style == "accent2")
        {
            color.role = GaugeColor::Accent2;
        }
    return color;
}

// "images/VX/Speed.png" as written on Windows -> the file as it is really spelled here.
QString findIgnoringCase(const QString &folder, const QString &relativePath)
{
    QDir dir(folder);
    const QStringList parts = relativePath.split('/', Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
        {
            const QString &part = parts.at(i);
            if (part == "..")
                {
                    return QString(); // nothing outside the gauge's folder
                }
            const bool last = i == parts.size() - 1;
            QString found;
            const QStringList entries = dir.entryList(last ? QDir::Files : QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &entry : entries)
                {
                    if (entry.compare(part, Qt::CaseInsensitive) == 0)
                        {
                            found = entry;
                            // an exact match wins over one that differs in case
                            if (entry == part)
                                {
                                    break;
                                }
                        }
                }
            if (found.isEmpty())
                {
                    return QString();
                }
            if (last)
                {
                    return dir.filePath(found);
                }
            dir.cd(found);
        }
    return QString();
}

// The manipulations of a component; `ok` is false when one of them cannot be
// done and the component would look wrong without it.
QVector<GaugeManipulation> manipulationsOf(const QJsonObject &object, Context &context, bool *ok)
{
    QJsonArray list = object.value("manipulations").toArray();
    if (object.contains("manipulation"))
        {
            list.append(object.value("manipulation"));
        }
    QVector<GaugeManipulation> result;
    for (const QJsonValue &value : list)
        {
            const QJsonObject source = value.toObject();
            const QString type = source.value("type").toString();
            GaugeManipulation m;
            if (type == "dynamicRotate")
                {
                    m.type = GaugeManipulation::Rotate;
                }
            else if (type == "dynamicRadialClip")
                {
                    m.type = GaugeManipulation::RadialClip;
                }
            else if (type == "dynamicTranslate" && source.contains("maxRadius"))
                {
                    m.type = GaugeManipulation::Translate2D;
                }
            else if (type == "dynamicTranslate")
                {
                    m.type = GaugeManipulation::Translate;
                }
            else if (type == "staticRadialClip")
                {
                    m.type = GaugeManipulation::StaticRadialClip;
                }
            else if (type == "animationStrip")
                {
                    m.type = GaugeManipulation::AnimationStrip;
                }
            else if (type == "dynamicWidthClip")
                {
                    m.type = GaugeManipulation::WidthClip;
                }
            else if (type == "dynamicHeightClip")
                {
                    m.type = GaugeManipulation::HeightClip;
                }
            else
                {
                    ++context.skipped[type.isEmpty() ? QStringLiteral("?") : type];
                    *ok = false;
                    continue;
                }
            m.pivot = QPointF(number(source, "pivotPointX"), number(source, "pivotPointY"));
            m.minAngle = number(source, "minAngle");
            m.maxAngle = number(source, "maxAngle");
            m.minX = number(source, "minX");
            m.maxX = number(source, "maxX");
            m.minY = number(source, "minY");
            m.maxY = number(source, "maxY");
            m.hasValueRange = source.contains("minValue") && source.contains("maxValue");
            m.minValue = number(source, "minValue");
            m.maxValue = number(source, "maxValue");
            m.wrapValue = flag(source, "wrapValue");
            m.reversed = source.value("moveDirection").toString() == "maxToMin";
            if (source.contains("clipRect"))
                {
                    m.clipRect = rectOf(source.value("clipRect").toObject());
                }
            m.clipAngle = number(source, "clipAngle");
            m.maxRadius = number(source, "maxRadius");
            m.invertX = flag(source, "invertXAxis");
            m.invertY = flag(source, "invertYAxis");
            if (m.type == GaugeManipulation::AnimationStrip)
                {
                    m.frames = std::max(1, int(number(source, "numberOfFrames")));
                    m.minFps = number(source, "minFramesPerSecond");
                    m.maxFps = number(source, "maxFramesPerSecond");
                    m.frameRect = rectOf(source.value("frameRect").toObject());
                }
            result.append(m);
        }
    return result;
}

void applyFont(const QString &sourceFamily, GaugeComponent *component)
{
    // "Arial Italic", "Roboto Bold", "Roboto Cn": style words ride along in the family name
    QStringList words = sourceFamily.split(' ', Qt::SkipEmptyParts);
    for (int i = words.size() - 1; i > 0; --i)
        {
            const QString word = words.at(i).toLower();
            if (word == "italic")
                {
                    component->italic = true;
                }
            else if (word == "bold" || word == "black")
                {
                    component->bold = true;
                }
            else if (word != "light" && word != "cn" && word != "regular")
                {
                    continue;
                }
            words.removeAt(i);
        }
    component->fontFamily = words.join(' ');
}

bool convert(const QJsonObject &source, Context &context, GaugeComponent *component);

void convertChildren(const QJsonArray &sources, Context &context, QVector<GaugeComponent> *components);

// A part given as one component ("lineMarkerComponent") or as a list ("trackKnobComponents").
QVector<GaugeComponent> partsOf(const QJsonObject &source, const char *key, Context &context)
{
    QVector<GaugeComponent> parts;
    const QJsonValue value = source.value(QLatin1String(key));
    convertChildren(value.isArray() ? value.toArray() : QJsonArray{value}, context, &parts);
    return parts;
}

void convertChildren(const QJsonArray &sources, Context &context, QVector<GaugeComponent> *components)
{
    for (const QJsonValue &value : sources)
        {
            if (!value.isObject())
                {
                    continue;
                }
            GaugeComponent component;
            if (convert(value.toObject(), context, &component))
                {
                    components->append(component);
                }
        }
}

bool convert(const QJsonObject &source, Context &context, GaugeComponent *component)
{
    const QString type = source.value("type").toString();
    if (flag(source, "isLogo"))
        {
            // the maker's trademark is not ours to draw
            ++context.skipped[QStringLiteral("logo")];
            return false;
        }
    if (flag(source, "hitTestingOnly"))
        {
            return false; // invisible in the old editor too
        }
    bool manipulationsOk = true;
    component->manipulations = manipulationsOf(source, context, &manipulationsOk);
    if (!manipulationsOk)
        {
            return false;
        }
    component->frame = rectOf(source.value("frame").toObject());
    component->color = colorStyle(source.value("colorStyle"));
    component->channel = VegGaugeImporter::channelFor(source.value("auxiliaryType").toString());
    component->hideWhenUnknown = flag(source, "hideWhenUnknown");

    if (type == "image")
        {
            component->type = GaugeComponent::Image;
            if (source.contains("autoImageType"))
                {
                    // the only kind there is: "icon", a picture of what is measured
                    component->channelIcon = true;
                    return true;
                }
            // written on Windows; QDir::fromNativeSeparators would leave the backslashes alone here
            component->imagePath = source.value("imagePath").toString().replace('\\', '/');
            if (!context.imageFolder.isEmpty())
                {
                    const QString file = findIgnoringCase(context.imageFolder, component->imagePath);
                    if (file.isEmpty() || !component->image.load(file))
                        {
                            context.missingImages << component->imagePath;
                            return false;
                        }
                }
            return true;
        }
    if (type == "text")
        {
            component->type = GaugeComponent::Text;
            if (source.contains("textPosition"))
                {
                    const QJsonObject position = source.value("textPosition").toObject();
                    component->frame = QRectF(number(position, "x"), number(position, "y"), 0, 0);
                }
            if (source.contains("fontColor"))
                {
                    component->color.role = GaugeColor::Custom;
                    component->color.custom = colorOf(source.value("fontColor").toObject());
                }
            const QString autoText = source.value("autoTextType").toString();
            component->autoText = autoText == "value" ? GaugeComponent::Value
                                  : autoText == "unit" ? GaugeComponent::Unit
                                  : autoText == "valueAndUnit" ? GaugeComponent::ValueAndUnit
                                  : autoText == "label" ? GaugeComponent::Label
                                  : autoText == "labelValueUnit" ? GaugeComponent::LabelValueUnit
                                  : autoText == "intervalMarking" ? GaugeComponent::IntervalMarking
                                  : GaugeComponent::FixedText;
            if (component->autoText == GaugeComponent::FixedText)
                {
                    component->text = source.value("textValue").toString();
                }
            applyFont(source.value("fontFamily").toString(), component);
            component->fontSize = source.contains("fontSize") ? number(source, "fontSize") : 24.0;
            component->precision = source.contains("valuePrecision") ? int(number(source, "valuePrecision")) : -1;
            const QString horizontal = source.value("horizontalAlign").toString();
            const QString vertical = source.value("verticalAlign").toString();
            component->alignment = (horizontal == "center" ? Qt::AlignHCenter : horizontal == "right" ? Qt::AlignRight : Qt::AlignLeft)
                                   | (vertical == "top" ? Qt::AlignTop : vertical == "bottom" ? Qt::AlignBottom : Qt::AlignVCenter);
            component->upperCase = source.value("casingTransform").toString() == "toUpper";
            return true;
        }
    if (type == "group")
        {
            component->type = GaugeComponent::Group;
            convertChildren(source.value("components").toArray(), context, &component->children);
            return !component->children.isEmpty();
        }
    if (type == "graphLine")
        {
            component->type = GaugeComponent::Graph;
            component->lineWidth = source.contains("lineWidth") ? number(source, "lineWidth") : 2.0;
            if (source.contains("bottomFillColor"))
                {
                    // often only the transparency: the fill then takes the line's colour
                    const QJsonObject fill = source.value("bottomFillColor").toObject();
                    component->fillAlpha = fill.contains("a") ? int(number(fill, "a")) : 255;
                    if (fill.contains("r") || fill.contains("g") || fill.contains("b"))
                        {
                            component->fillColor = QColor(int(number(fill, "r")), int(number(fill, "g")), int(number(fill, "b")));
                        }
                }
            component->knob = partsOf(source, "lineMarkerComponent", context);
            component->xMarker = partsOf(source, "xAxisMarkerComponent", context);
            component->yMarker = partsOf(source, "yAxisMarkerComponent", context);
            return true;
        }
    if (type == "trackLine" || type == "lapCircuit")
        {
            component->type = type == "trackLine" ? GaugeComponent::TrackMap : GaugeComponent::LapMap;
            component->lineWidth = source.contains("thickness") ? number(source, "thickness") : 2.0;
            component->padding = number(source, "padding");
            component->knob = partsOf(source, "trackKnobComponents", context);
            component->startLine = partsOf(source, "startLineComponents", context);
            return true;
        }
    ++context.skipped[type.isEmpty() ? QStringLiteral("?") : type];
    return false;
}

QStringList channelsFor(const QJsonValue &types)
{
    QStringList channels;
    for (const QJsonValue &type : types.toArray())
        {
            const QString channel = VegGaugeImporter::channelFor(type.toString());
            if (!channel.isEmpty() && !channels.contains(channel))
                {
                    channels << channel;
                }
        }
    return channels;
}
}

QString VegGaugeImporter::channelFor(const QString &sourceType)
{
    static const QHash<QString, QString> aliases =
    {
        {"speed2d", ChannelId::Speed}, {"speed3d", ChannelId::Speed}, {"vehicleSpeed", ChannelId::Speed},
        {"speedOverGround", ChannelId::Speed},
        {"compass", ChannelId::Heading}, {"magneticHeading", ChannelId::Heading},
        {"trueCourseOverGround", ChannelId::Heading},
    };
    return aliases.value(sourceType, sourceType);
}

QString VegGaugeImporter::nameFromFileName(const QString &baseName)
{
    QString name = baseName;
    name.replace('_', ' ');
    return name.simplified();
}

VegGaugeImporter::Result VegGaugeImporter::parse(const QByteArray &json, const QString &name, const QString &imageFolder)
{
    Result result;
    // the files start with a byte order mark, which the JSON parser refuses
    const QByteArray text = json.startsWith("\xEF\xBB\xBF") ? json.mid(3) : json;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text, &parseError);
    if (!document.isObject())
        {
            result.error = tr("This is not a .veg gauge: %1").arg(parseError.errorString());
            return result;
        }
    const QJsonObject root = document.object();
    const QJsonObject frame = root.value("frame").toObject();
    result.gauge.size = QSizeF(number(frame, "width"), number(frame, "height"));
    if (result.gauge.size.isEmpty() || !root.value("components").isArray())
        {
            result.error = tr("This is not a .veg gauge: it has no size or no components.");
            return result;
        }
    result.gauge.name = name;
    result.gauge.supportedTypes = channelsFor(root.value("supportedTypes"));
    result.gauge.unsupportedTypes = channelsFor(root.value("unsupportedTypes"));

    Context context;
    context.imageFolder = imageFolder;
    convertChildren(root.value("components").toArray(), context, &result.gauge.components);

    for (auto it = context.skipped.constBegin(); it != context.skipped.constEnd(); ++it)
        {
            result.warnings << tr("Left out, not supported yet: %1 (%2x)").arg(it.key()).arg(it.value());
        }
    context.missingImages.removeDuplicates();
    for (const QString &image : context.missingImages)
        {
            result.warnings << tr("Image not found: %1").arg(image);
        }
    if (result.gauge.components.isEmpty())
        {
            result.error = tr("Nothing of this gauge can be shown yet.");
            return result;
        }
    result.ok = true;
    return result;
}

VegGaugeImporter::Result VegGaugeImporter::importFile(const QString &vegPath)
{
    QFile file(vegPath);
    if (!file.open(QIODevice::ReadOnly))
        {
            Result result;
            result.error = tr("Cannot open %1: %2").arg(vegPath, file.errorString());
            return result;
        }
    const QFileInfo info(vegPath);
    return parse(file.readAll(), nameFromFileName(info.completeBaseName()), info.absolutePath());
}
