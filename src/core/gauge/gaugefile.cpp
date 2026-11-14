#include "gaugefile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const char kFormat[] = "rtvgauge";
const int kVersion = 1;

const char *const kComponentTypes[] = {"image", "text", "group", "graph", "trackMap", "lapMap", "shape", "dial", "tape"};
const char *const kAutoTexts[] = {"fixed", "value", "unit", "valueAndUnit", "label", "labelValueUnit", "intervalMarking"};
const char *const kShapeKinds[] = {"rectangle", "ellipse", "arc", "pie", "line"};
const char *const kValueFormats[] = {"number", "wholeNumber", "duration", "degreesMinutesSeconds", "clock"};
const char *const kTapeOrientations[] = {"numbersAbove", "numbersBelow", "numbersLeft", "numbersRight"};
const char *const kManipulationTypes[] = {"rotate", "radialClip", "translate", "widthClip", "heightClip",
                                          "staticRadialClip", "translate2d", "animationStrip"
                                         };
const char *const kColorRoles[] = {"original", "foreground", "background", "accent", "accent2"};

template <int N>
int indexOf(const char *const (&words)[N], const QString &word, int fallback = 0)
{
    for (int i = 0; i < N; ++i)
        {
            if (word == QLatin1String(words[i]))
                {
                    return i;
                }
        }
    return fallback;
}

QJsonArray toArray(const QRectF &rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

QRectF toRect(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return QRectF(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble());
}

QJsonValue colorToJson(const GaugeColor &color)
{
    return color.role == GaugeColor::Custom ? QJsonValue(color.custom.name(QColor::HexArgb))
           : QJsonValue(QLatin1String(kColorRoles[color.role]));
}

GaugeColor colorFromJson(const QJsonValue &value)
{
    GaugeColor color;
    const QString text = value.toString();
    if (text.startsWith('#'))
        {
            color.role = GaugeColor::Custom;
            color.custom = QColor(text);
        }
    else
        {
            color.role = GaugeColor::Role(indexOf(kColorRoles, text));
        }
    return color;
}

QJsonArray pointsToJson(const QVector<QPointF> &points)
{
    QJsonArray array;
    for (const QPointF &p : points)
        {
            array.append(QJsonArray{p.x(), p.y()});
        }
    return array;
}

QVector<QPointF> pointsFromJson(const QJsonValue &value)
{
    QVector<QPointF> points;
    for (const QJsonValue &item : value.toArray())
        {
            const QJsonArray p = item.toArray();
            points.append(QPointF(p.at(0).toDouble(), p.at(1).toDouble()));
        }
    return points;
}

QJsonObject ticksToJson(const GaugeTicks &ticks)
{
    return {{"unit", ticks.unit}, {"length", ticks.length}, {"width", ticks.width}, {"color", colorToJson(ticks.color)}};
}

GaugeTicks ticksFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    GaugeTicks ticks;
    ticks.unit = object.value("unit").toDouble();
    ticks.length = object.value("length").toDouble();
    ticks.width = object.value("width").toDouble(1.0);
    ticks.color = colorFromJson(object.value("color"));
    return ticks;
}

QJsonObject scaleToJson(const GaugeScale &s, GaugeComponent::Type type)
{
    QJsonObject object
    {
        {"minValue", s.minValue}, {"maxValue", s.maxValue},
        {"minor", ticksToJson(s.minor)}, {"major", ticksToJson(s.major)},
        {"labelOffset", s.labelOffset},
    };
    if (type == GaugeComponent::Dial)
        {
            object["startAngle"] = s.startAngle;
            object["endAngle"] = s.endAngle;
            object["radius"] = s.radius;
            if (s.arcWidth > 0.0)
                {
                    object["arcWidth"] = s.arcWidth;
                    object["arcColor"] = colorToJson(s.arcColor);
                }
        }
    else
        {
            object["orientation"] = kTapeOrientations[s.orientation];
            object["length"] = s.length;
            object["spacing"] = s.spacing;
            if (s.loop)
                {
                    object["loop"] = true;
                }
            if (s.reverse)
                {
                    object["reverse"] = true;
                }
            if (s.markerLength > 0.0)
                {
                    object["markerLength"] = s.markerLength;
                    object["markerWidth"] = s.markerWidth;
                    object["markerColor"] = colorToJson(s.markerColor);
                }
        }
    return object;
}

GaugeScale scaleFromJson(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    GaugeScale s;
    s.minValue = object.value("minValue").toDouble(s.minValue);
    s.maxValue = object.value("maxValue").toDouble(s.maxValue);
    s.loop = object.value("loop").toBool();
    s.minor = ticksFromJson(object.value("minor"));
    s.major = ticksFromJson(object.value("major"));
    s.labelOffset = object.value("labelOffset").toDouble();
    s.startAngle = object.value("startAngle").toDouble();
    s.endAngle = object.value("endAngle").toDouble();
    s.radius = object.value("radius").toDouble();
    s.arcWidth = object.value("arcWidth").toDouble();
    s.arcColor = colorFromJson(object.value("arcColor"));
    s.orientation = GaugeScale::Orientation(indexOf(kTapeOrientations, object.value("orientation").toString()));
    s.reverse = object.value("reverse").toBool();
    s.length = object.value("length").toDouble(s.length);
    s.spacing = object.value("spacing").toDouble(s.spacing);
    s.markerLength = object.value("markerLength").toDouble();
    s.markerWidth = object.value("markerWidth").toDouble(1.0);
    s.markerColor = colorFromJson(object.value("markerColor"));
    return s;
}

QJsonObject fontToJson(const GaugeComponent &c)
{
    QJsonObject font{{"size", c.fontSize}};
    if (!c.fontFamily.isEmpty())
        {
            font["family"] = c.fontFamily;
        }
    if (c.bold)
        {
            font["bold"] = true;
        }
    if (c.italic)
        {
            font["italic"] = true;
        }
    return font;
}

QJsonObject manipulationToJson(const GaugeManipulation &m)
{
    QJsonObject object{{"type", kManipulationTypes[m.type]}};
    switch (m.type)
        {
        case GaugeManipulation::Rotate:
        case GaugeManipulation::RadialClip:
        case GaugeManipulation::StaticRadialClip:
            object["pivot"] = QJsonArray{m.pivot.x(), m.pivot.y()};
            object["minAngle"] = m.minAngle;
            object["maxAngle"] = m.maxAngle;
            break;
        case GaugeManipulation::Translate:
            object["minX"] = m.minX;
            object["maxX"] = m.maxX;
            object["minY"] = m.minY;
            object["maxY"] = m.maxY;
            break;
        case GaugeManipulation::WidthClip:
            object["minX"] = m.minX;
            object["maxX"] = m.maxX;
            break;
        case GaugeManipulation::HeightClip:
            object["minY"] = m.minY;
            object["maxY"] = m.maxY;
            break;
        case GaugeManipulation::Translate2D:
            object["maxRadius"] = m.maxRadius;
            if (m.invertX)
                {
                    object["invertX"] = true;
                }
            if (m.invertY)
                {
                    object["invertY"] = true;
                }
            if (!m.xChannel.isEmpty())
                {
                    object["xChannel"] = m.xChannel;
                }
            if (!m.yChannel.isEmpty())
                {
                    object["yChannel"] = m.yChannel;
                }
            break;
        case GaugeManipulation::AnimationStrip:
            object["frames"] = m.frames;
            object["minFps"] = m.minFps;
            object["maxFps"] = m.maxFps;
            object["frameRect"] = toArray(m.frameRect);
            break;
        }
    if (m.clipAngle != 0.0)
        {
            object["clipAngle"] = m.clipAngle;
        }
    if (m.hasValueRange)
        {
            object["minValue"] = m.minValue;
            object["maxValue"] = m.maxValue;
        }
    if (m.recordingRange)
        {
            object["range"] = "recording";
        }
    if (m.wrapValue)
        {
            object["wrap"] = true;
        }
    if (m.reversed)
        {
            object["reversed"] = true;
        }
    if (!m.curve.isEmpty())
        {
            object["curve"] = pointsToJson(m.curve);
        }
    if (!m.clipRect.isNull())
        {
            object["clip"] = toArray(m.clipRect);
        }
    return object;
}

GaugeManipulation manipulationFromJson(const QJsonObject &object)
{
    GaugeManipulation m;
    m.type = GaugeManipulation::Type(indexOf(kManipulationTypes, object.value("type").toString()));
    const QJsonArray pivot = object.value("pivot").toArray();
    m.pivot = QPointF(pivot.at(0).toDouble(), pivot.at(1).toDouble());
    m.minAngle = object.value("minAngle").toDouble();
    m.maxAngle = object.value("maxAngle").toDouble();
    m.minX = object.value("minX").toDouble();
    m.maxX = object.value("maxX").toDouble();
    m.minY = object.value("minY").toDouble();
    m.maxY = object.value("maxY").toDouble();
    m.hasValueRange = object.contains("minValue") && object.contains("maxValue");
    m.minValue = object.value("minValue").toDouble();
    m.maxValue = object.value("maxValue").toDouble();
    m.recordingRange = object.value("range").toString() == "recording";
    m.wrapValue = object.value("wrap").toBool();
    m.reversed = object.value("reversed").toBool();
    m.curve = pointsFromJson(object.value("curve"));
    if (object.contains("clip"))
        {
            m.clipRect = toRect(object.value("clip"));
        }
    m.clipAngle = object.value("clipAngle").toDouble();
    m.maxRadius = object.value("maxRadius").toDouble();
    m.invertX = object.value("invertX").toBool();
    m.invertY = object.value("invertY").toBool();
    m.xChannel = object.value("xChannel").toString();
    m.yChannel = object.value("yChannel").toString();
    m.frames = object.value("frames").toInt(1);
    m.minFps = object.value("minFps").toDouble(1.0);
    m.maxFps = object.value("maxFps").toDouble(1.0);
    if (object.contains("frameRect"))
        {
            m.frameRect = toRect(object.value("frameRect"));
        }
    return m;
}

QJsonObject componentToJson(const GaugeComponent &c);

QJsonArray componentsToJson(const QVector<GaugeComponent> &components)
{
    QJsonArray array;
    for (const GaugeComponent &c : components)
        {
            array.append(componentToJson(c));
        }
    return array;
}

QJsonObject componentToJson(const GaugeComponent &c)
{
    QJsonObject object{{"type", kComponentTypes[c.type]}};
    if (c.type != GaugeComponent::Group)
        {
            object["frame"] = c.type == GaugeComponent::Text && !c.boxed ? QJsonArray{c.frame.x(), c.frame.y()} : toArray(c.frame);
        }
    if (c.color.role != GaugeColor::Original)
        {
            object["color"] = colorToJson(c.color);
        }
    if (!c.channel.isEmpty())
        {
            object["channel"] = c.channel;
        }
    if (!c.unit.isEmpty())
        {
            object["unit"] = c.unit;
        }
    if (c.hideWhenUnknown)
        {
            object["hideWhenUnknown"] = true;
        }
    if (!c.manipulations.isEmpty())
        {
            QJsonArray manipulations;
            for (const GaugeManipulation &m : c.manipulations)
                {
                    manipulations.append(manipulationToJson(m));
                }
            object["manipulations"] = manipulations;
        }
    switch (c.type)
        {
        case GaugeComponent::Image:
            if (c.channelIcon)
                {
                    object["channelIcon"] = true;
                }
            else
                {
                    object["image"] = c.imagePath;
                }
            break;
        case GaugeComponent::Text:
        {
            object["autoText"] = kAutoTexts[c.autoText];
            if (!c.text.isEmpty())
                {
                    object["text"] = c.text;
                }
            object["font"] = fontToJson(c);
            if (c.precision >= 0)
                {
                    object["precision"] = c.precision;
                }
            object["align"] = c.alignment & Qt::AlignHCenter ? "center" : c.alignment & Qt::AlignRight ? "right" : "left";
            object["valign"] = c.alignment & Qt::AlignTop ? "top" : c.alignment & Qt::AlignBottom ? "bottom" : "center";
            if (c.upperCase)
                {
                    object["upperCase"] = true;
                }
            if (c.boxed)
                {
                    object["boxed"] = true;
                }
            if (c.valueFormat != GaugeComponent::Number)
                {
                    object["format"] = kValueFormats[c.valueFormat];
                }
            if (c.valueFormat == GaugeComponent::Clock)
                {
                    object["clockFormat"] = c.clockFormat;
                    if (!c.localTime)
                        {
                            object["utc"] = true;
                        }
                }
            if (c.showPlus)
                {
                    object["showPlus"] = true;
                }
            if (c.clampValue)
                {
                    object["clamp"] = QJsonArray{c.clampMin, c.clampMax};
                }
            break;
        }
        case GaugeComponent::Group:
            object["components"] = componentsToJson(c.children);
            break;
        case GaugeComponent::Shape:
            object["shape"] = kShapeKinds[c.shape];
            if (c.shape == GaugeComponent::Line)
                {
                    object.remove("frame");
                    object["points"] = pointsToJson(c.points);
                }
            if (c.shape == GaugeComponent::Line || c.shape == GaugeComponent::Arc || !c.filled)
                {
                    object["lineWidth"] = c.lineWidth;
                }
            if (!c.filled && c.shape != GaugeComponent::Line && c.shape != GaugeComponent::Arc)
                {
                    object["filled"] = false;
                }
            if (c.cornerRadius > 0.0)
                {
                    object["cornerRadius"] = c.cornerRadius;
                }
            if (c.shape == GaugeComponent::Arc || c.shape == GaugeComponent::Pie)
                {
                    object["startAngle"] = c.startAngle;
                    object["sweepAngle"] = c.sweepAngle;
                }
            if (c.gradientColor.role != GaugeColor::Original)
                {
                    object["gradientColor"] = colorToJson(c.gradientColor);
                    object["gradientLine"] = QJsonArray{c.gradientLine.x1(), c.gradientLine.y1(),
                                                        c.gradientLine.x2(), c.gradientLine.y2()};
                }
            break;
        case GaugeComponent::Dial:
        case GaugeComponent::Tape:
            object["scale"] = scaleToJson(c.scale, c.type);
            object["font"] = fontToJson(c);
            object["align"] = c.alignment & Qt::AlignHCenter ? "center" : c.alignment & Qt::AlignRight ? "right" : "left";
            object["valign"] = c.alignment & Qt::AlignTop ? "top" : c.alignment & Qt::AlignBottom ? "bottom" : "center";
            break;
        case GaugeComponent::Graph:
        case GaugeComponent::TrackMap:
        case GaugeComponent::LapMap:
            object["lineWidth"] = c.lineWidth;
            if (c.windowBefore > 0.0 || c.windowAfter > 0.0)
                {
                    object["window"] = QJsonArray{c.windowBefore, c.windowAfter};
                }
            if (c.padding != 0.0)
                {
                    object["padding"] = c.padding;
                }
            if (c.fillAlpha >= 0)
                {
                    object["fillAlpha"] = c.fillAlpha;
                    if (c.fillColor.isValid())
                        {
                            object["fillColor"] = c.fillColor.name(QColor::HexRgb);
                        }
                }
            if (!c.knob.isEmpty())
                {
                    object["knob"] = componentsToJson(c.knob);
                }
            if (!c.xMarker.isEmpty())
                {
                    object["xMarker"] = componentsToJson(c.xMarker);
                }
            if (!c.yMarker.isEmpty())
                {
                    object["yMarker"] = componentsToJson(c.yMarker);
                }
            if (!c.startLine.isEmpty())
                {
                    object["startLine"] = componentsToJson(c.startLine);
                }
            break;
        }
    return object;
}

GaugeComponent componentFromJson(const QJsonObject &object);

QVector<GaugeComponent> componentsFromJson(const QJsonValue &value)
{
    QVector<GaugeComponent> components;
    for (const QJsonValue &item : value.toArray())
        {
            components.append(componentFromJson(item.toObject()));
        }
    return components;
}

GaugeComponent componentFromJson(const QJsonObject &object)
{
    GaugeComponent c;
    c.type = GaugeComponent::Type(indexOf(kComponentTypes, object.value("type").toString()));
    const QJsonArray frame = object.value("frame").toArray();
    c.frame = frame.size() >= 4 ? toRect(frame) : QRectF(frame.at(0).toDouble(), frame.at(1).toDouble(), 0, 0);
    c.color = colorFromJson(object.value("color"));
    c.channel = object.value("channel").toString();
    c.unit = object.value("unit").toString();
    c.hideWhenUnknown = object.value("hideWhenUnknown").toBool();
    const QJsonArray manipulations = object.value("manipulations").toArray();
    for (const QJsonValue &m : manipulations)
        {
            c.manipulations.append(manipulationFromJson(m.toObject()));
        }
    c.imagePath = object.value("image").toString();
    c.channelIcon = object.value("channelIcon").toBool();
    c.autoText = GaugeComponent::AutoText(indexOf(kAutoTexts, object.value("autoText").toString()));
    c.text = object.value("text").toString();
    const QJsonObject font = object.value("font").toObject();
    c.fontFamily = font.value("family").toString();
    c.fontSize = font.value("size").toDouble(c.fontSize);
    c.bold = font.value("bold").toBool();
    c.italic = font.value("italic").toBool();
    c.precision = object.value("precision").toInt(-1);
    const QString align = object.value("align").toString();
    const QString valign = object.value("valign").toString();
    c.alignment = (align == "center" ? Qt::AlignHCenter : align == "right" ? Qt::AlignRight : Qt::AlignLeft)
                  | (valign == "top" ? Qt::AlignTop : valign == "bottom" ? Qt::AlignBottom : Qt::AlignVCenter);
    c.upperCase = object.value("upperCase").toBool();
    c.boxed = object.value("boxed").toBool();
    c.valueFormat = GaugeComponent::ValueFormat(indexOf(kValueFormats, object.value("format").toString()));
    c.clockFormat = object.value("clockFormat").toString();
    c.localTime = !object.value("utc").toBool();
    c.showPlus = object.value("showPlus").toBool();
    const QJsonArray clamp = object.value("clamp").toArray();
    c.clampValue = clamp.size() == 2;
    c.clampMin = clamp.at(0).toDouble();
    c.clampMax = clamp.at(1).toDouble();
    c.children = componentsFromJson(object.value("components"));
    c.shape = GaugeComponent::ShapeKind(indexOf(kShapeKinds, object.value("shape").toString()));
    c.points = pointsFromJson(object.value("points"));
    // arcs and lines are only ever drawn with a pen
    c.filled = object.value("filled").toBool(true) && c.shape != GaugeComponent::Arc && c.shape != GaugeComponent::Line;
    c.cornerRadius = object.value("cornerRadius").toDouble();
    c.startAngle = object.value("startAngle").toDouble();
    c.sweepAngle = object.value("sweepAngle").toDouble();
    c.gradientColor = colorFromJson(object.value("gradientColor"));
    const QJsonArray gradient = object.value("gradientLine").toArray();
    c.gradientLine = QLineF(gradient.at(0).toDouble(), gradient.at(1).toDouble(), gradient.at(2).toDouble(), gradient.at(3).toDouble());
    c.scale = scaleFromJson(object.value("scale"));
    const QJsonArray window = object.value("window").toArray();
    c.windowBefore = window.at(0).toDouble();
    c.windowAfter = window.at(1).toDouble();
    c.lineWidth = object.value("lineWidth").toDouble(c.lineWidth);
    c.padding = object.value("padding").toDouble();
    c.fillAlpha = object.value("fillAlpha").toInt(-1);
    if (object.contains("fillColor"))
        {
            c.fillColor = QColor(object.value("fillColor").toString());
        }
    c.knob = componentsFromJson(object.value("knob"));
    c.xMarker = componentsFromJson(object.value("xMarker"));
    c.yMarker = componentsFromJson(object.value("yMarker"));
    c.startLine = componentsFromJson(object.value("startLine"));
    return c;
}

// A path that came out of a file must stay inside the gauge's folder.
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || QFileInfo(path).isAbsolute() || path.contains(':'))
        {
            return false;
        }
    const QString clean = QDir::cleanPath(path);
    return clean != ".." && !clean.startsWith("../");
}

template <typename Function>
bool forEachImage(QVector<GaugeComponent> &components, const Function &function)
{
    for (GaugeComponent &c : components)
        {
            if (c.type == GaugeComponent::Image && !c.channelIcon && !function(c))
                {
                    return false;
                }
            for (QVector<GaugeComponent> *parts : {&c.children, &c.knob, &c.xMarker, &c.yMarker, &c.startLine})
                {
                    if (!forEachImage(*parts, function))
                        {
                            return false;
                        }
                }
        }
    return true;
}
}

QByteArray GaugeFile::toJson(const Gauge &gauge)
{
    const QJsonArray components = componentsToJson(gauge.components);
    QJsonObject root
    {
        {"format", kFormat},
        {"version", kVersion},
        {"name", gauge.name},
        {"size", QJsonArray{gauge.size.width(), gauge.size.height()}},
        {"components", components},
    };
    if (!gauge.supportedTypes.isEmpty())
        {
            root["supportedTypes"] = QJsonArray::fromStringList(gauge.supportedTypes);
        }
    if (!gauge.unsupportedTypes.isEmpty())
        {
            root["unsupportedTypes"] = QJsonArray::fromStringList(gauge.unsupportedTypes);
        }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool GaugeFile::fromJson(const QByteArray &json, Gauge *gauge, QString *error)
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
            return fail(tr("This is not a gauge file: %1").arg(parseError.errorString()));
        }
    const QJsonObject root = document.object();
    if (root.value("format").toString() != QLatin1String(kFormat))
        {
            return fail(tr("This is not a Race Track Visualizer gauge file."));
        }
    if (root.value("version").toInt() > kVersion)
        {
            return fail(tr("The gauge was saved by a newer version of the program (file version %1).")
                        .arg(root.value("version").toInt()));
        }
    Gauge loaded;
    loaded.name = root.value("name").toString();
    const QJsonArray size = root.value("size").toArray();
    loaded.size = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
    for (const QJsonValue &type : root.value("supportedTypes").toArray())
        {
            loaded.supportedTypes << type.toString();
        }
    for (const QJsonValue &type : root.value("unsupportedTypes").toArray())
        {
            loaded.unsupportedTypes << type.toString();
        }
    loaded.components = componentsFromJson(root.value("components"));
    if (loaded.size.isEmpty())
        {
            return fail(tr("The gauge has no size."));
        }
    *gauge = loaded;
    return true;
}

bool GaugeFile::load(const QString &folder, Gauge *gauge, QString *error)
{
    const QDir dir(folder);
    QFile file(dir.filePath(FileName));
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = tr("Cannot open %1: %2").arg(file.fileName(), file.errorString());
                }
            return false;
        }
    Gauge loaded;
    if (!fromJson(file.readAll(), &loaded, error))
        {
            return false;
        }
    // a missing picture leaves a hole in the gauge, it does not make it unusable
    forEachImage(loaded.components, [&dir](GaugeComponent &c)
    {
        if (!isSafeRelativePath(c.imagePath))
            {
                return true;
            }
        if (c.imagePath.endsWith(".svg", Qt::CaseInsensitive))
            {
                QFile svg(dir.filePath(c.imagePath));
                if (svg.open(QIODevice::ReadOnly))
                    {
                        c.svg = svg.readAll();
                    }
            }
        else
            {
                c.image = QImage(dir.filePath(c.imagePath));
            }
        return true;
    });
    *gauge = loaded;
    return true;
}

bool GaugeFile::save(const QString &folder, const Gauge &gauge, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return false;
    };
    QDir dir(folder);
    if (!dir.mkpath("."))
        {
            return fail(tr("Cannot create the folder %1.").arg(folder));
        }
    Gauge copy = gauge;
    int unnamed = 0;
    QString failedImage;
    const bool imagesWritten = forEachImage(copy.components, [&](GaugeComponent &c)
    {
        const bool drawing = !c.svg.isEmpty();
        if (c.image.isNull() && !drawing)
            {
                return true;
            }
        if (!isSafeRelativePath(c.imagePath) || drawing != c.imagePath.endsWith(".svg", Qt::CaseInsensitive))
            {
                c.imagePath = QString("images/image%1.%2").arg(++unnamed).arg(drawing ? "svg" : "png");
            }
        const QString target = dir.filePath(c.imagePath);
        // the same picture is often used by several components
        if (QFileInfo::exists(target))
            {
                return true;
            }
        auto write = [&]()
        {
            if (!drawing)
                {
                    return c.image.save(target);
                }
            QSaveFile file(target);
            return file.open(QIODevice::WriteOnly) && file.write(c.svg) == c.svg.size() && file.commit();
        };
        if (!dir.mkpath(QFileInfo(c.imagePath).path()) || !write())
            {
                failedImage = target;
                return false;
            }
        return true;
    });
    if (!imagesWritten)
        {
            return fail(tr("Cannot write %1.").arg(failedImage));
        }
    QSaveFile file(dir.filePath(FileName));
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson(copy)) < 0 || !file.commit())
        {
            return fail(tr("Cannot write %1: %2").arg(file.fileName(), file.errorString()));
        }
    return true;
}
