#include "xmlgaugeimporter.h"

#include "../telemetry/channelids.h"
#include "../telemetry/datatypes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace
{
// Font sizes are points at 96 dots per inch.
const double kPointsToPixels = 96.0 / 72.0;

// An element with everything inside it; the files are small.
struct Element
{
    QString name;
    QHash<QString, QString> attributes;
    QVector<Element> children;
    QString text;

    bool has(const QString &key) const { return attributes.contains(key); }
    QString string(const QString &key, const QString &fallback = QString()) const { return attributes.value(key, fallback); }
    double number(const QString &key, double fallback = 0.0) const
    {
        bool ok = false;
        // written with the invariant culture: a point for decimals
        const double value = attributes.value(key).toDouble(&ok);
        return ok && std::isfinite(value) ? value : fallback;
    }
    bool flag(const QString &key, bool fallback = false) const
    {
        const QString value = attributes.value(key);
        return value.isEmpty() ? fallback : value.compare("true", Qt::CaseInsensitive) == 0;
    }
    const Element *child(const QString &childName) const
    {
        for (const Element &c : children)
            {
                if (c.name == childName)
                    {
                        return &c;
                    }
            }
        return nullptr;
    }
};

bool readElement(QXmlStreamReader &reader, Element *element)
{
    element->name = reader.name().toString();
    for (const QXmlStreamAttribute &attribute : reader.attributes())
        {
            element->attributes.insert(attribute.name().toString(), attribute.value().toString());
        }
    while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isStartElement())
                {
                    Element child;
                    if (!readElement(reader, &child))
                        {
                            return false;
                        }
                    element->children.append(child);
                }
            else if (reader.isCharacters())
                {
                    element->text += reader.text();
                }
            else if (reader.isEndElement())
                {
                    element->text = element->text.trimmed();
                    return true;
                }
        }
    return !reader.hasError();
}

// "red" or "arcRed": attribute names carry the part they colour in front.
QString key(const QString &prefix, const char *name)
{
    if (prefix.isEmpty())
        {
            QString lower = QString::fromLatin1(name);
            lower[0] = lower.at(0).toLower();
            return lower;
        }
    return prefix + QLatin1String(name);
}

GaugeColor custom(const QColor &color)
{
    GaugeColor c;
    c.role = GaugeColor::Custom;
    c.custom = color;
    return c;
}

QString scaleNumber(double value)
{
    return QString::number(std::fabs(value) < 1e-9 ? 0.0 : value, 'g', 12);
}

// The file name as it is on disk; the gauges were made on Windows.
QString findFile(const QString &folder, const QString &name)
{
    const QDir dir(folder);
    if (dir.exists(name))
        {
            return dir.filePath(name);
        }
    const QString wanted = QDir::fromNativeSeparators(name);
    for (const QString &entry : dir.entryList(QDir::Files))
        {
            if (entry.compare(wanted, Qt::CaseInsensitive) == 0)
                {
                    return dir.filePath(entry);
                }
        }
    return QString();
}

class Converter
{
public:
    Converter(double opacity, const QString &imageFolder)
        : m_opacity(opacity)
        , m_imageFolder(imageFolder)
    {
    }

    QHash<QString, QString> inputTypes;   // the gauge's input names -> data types
    QString mainChannel;
    QString mainUnit;
    QStringList warnings;

    void convertAll(const Element &parent, const QPointF &origin, const Element *face, QVector<GaugeComponent> *out)
    {
        if (const Element *components = parent.child("Components"))
            {
                for (const Element &e : components->children)
                    {
                        convert(e, origin, face, out);
                    }
            }
    }

private:
    double m_opacity{1.0};
    QString m_imageFolder;

    void warn(const QString &message)
    {
        if (!warnings.contains(message))
            {
                warnings << message;
            }
    }

    QColor color(const Element &e, const QString &prefix) const
    {
        const double alpha = std::clamp(e.number(key(prefix, "Transparency"), 1.0) * m_opacity, 0.0, 1.0);
        return QColor(std::clamp(int(e.number(key(prefix, "Red"), 255)), 0, 255),
                      std::clamp(int(e.number(key(prefix, "Green"), 255)), 0, 255),
                      std::clamp(int(e.number(key(prefix, "Blue"), 255)), 0, 255), int(std::lround(alpha * 255.0)));
    }

    double penWidth(const Element &e, const QString &prefix) const
    {
        return e.number(key(prefix, "PenWidth"), 1.0);
    }

    // Font, colour and alignment of a text. A text with a rectangle is
    // aligned inside it; one at a point is placed beside the point, on the
    // side its alignment names.
    void applyText(const Element &e, const QString &prefix, bool boxed, GaugeComponent *c)
    {
        const QString p = prefix.isEmpty() ? QString() : prefix + '_';
        c->fontFamily = e.string(p + "fontFace", "Arial");
        c->fontSize = e.number(p + "fontSize", 8.0) * kPointsToPixels;
        c->bold = e.flag(p + "fontBold");
        c->italic = e.flag(p + "fontItalic");
        c->color = custom(color(e, p + "font"));
        const QString align = e.string(p + "fontAlign");
        const QString valign = e.string(p + "fontVAlign");
        Qt::Alignment h = Qt::AlignHCenter;
        Qt::Alignment v = Qt::AlignVCenter;
        if (align == "left")
            {
                h = boxed ? Qt::AlignLeft : Qt::AlignRight;
            }
        else if (align == "right")
            {
                h = boxed ? Qt::AlignRight : Qt::AlignLeft;
            }
        if (valign == "top")
            {
                v = boxed ? Qt::AlignTop : Qt::AlignBottom;
            }
        else if (valign == "bottom")
            {
                v = boxed ? Qt::AlignBottom : Qt::AlignTop;
            }
        c->alignment = h | v;
        if (e.string(p + "fontDirection") == "vertical")
            {
                warn(XmlGaugeImporter::tr("vertical text is drawn horizontally"));
            }
    }

    // The channel and unit an input of a part reads.
    void applyInput(const Element &e, const QString &inputName, GaugeComponent *c)
    {
        QString unit;
        const QString channel = channelOf(e, inputName, &unit);
        c->channel = channel == mainChannel && unit == mainUnit ? QString() : channel;
        c->unit = unit;
    }

    QString channelOf(const Element &e, const QString &inputName, QString *unit)
    {
        const Element *inputs = e.child("Inputs");
        if (!inputs)
            {
                return QString();
            }
        for (const Element &input : inputs->children)
            {
                if (input.string("name") == inputName)
                    {
                        const QString type = inputTypes.value(input.text, input.text);
                        if (input.flag("fromFastestLapSamePos"))
                            {
                                warn(XmlGaugeImporter::tr("values from the fastest lap are not there yet"));
                            }
                        return XmlGaugeImporter::channelFor(type, unit);
                    }
            }
        return QString();
    }

    // The value scale of a needle or arc, with the angles it turns through.
    // Parts inside a dial may take either from the dial.
    GaugeManipulation range(const Element &e, const Element *face, double *minAngle, double *maxAngle)
    {
        GaugeManipulation m;
        double minValue = e.number("minValue");
        double maxValue = e.number("maxValue", 100.0);
        *minAngle = e.number("minAngle");
        *maxAngle = e.number("maxAngle");
        if (face && (e.flag("syncValueRangeWithParent") || e.flag("syncWithParent")))
            {
                minValue = face->number("minValue");
                maxValue = face->number("maxValue", 100.0);
            }
        if (face && (e.flag("syncAngleRangeWithParent") || e.flag("syncWithParent")))
            {
                *minAngle = face->number("minAngle");
                *maxAngle = face->number("maxAngle");
            }
        m.hasValueRange = true;
        m.minValue = minValue;
        m.maxValue = maxValue;
        m.wrapValue = e.flag("loop");
        const int steps = int(e.number("stepCount"));
        if (steps > 0)
            {
                if (maxValue > minValue && *maxAngle != *minAngle)
                    {
                        QVector<QPointF> curve{{minValue, 0.0}};
                        for (int i = 0; i < steps; ++i)
                            {
                                const double value = e.number(QString("stepValue%1").arg(i));
                                const double angle = e.number(QString("stepAngle%1").arg(i));
                                if (value > minValue && value < maxValue)
                                    {
                                        curve.append(QPointF(value, (angle - *minAngle) / (*maxAngle - *minAngle)));
                                    }
                            }
                        curve.append(QPointF(maxValue, 1.0));
                        std::sort(curve.begin(), curve.end(), [](const QPointF &a, const QPointF &b)
                        {
                            return a.x() < b.x();
                        });
                        m.curve = curve;
                    }
                else
                    {
                        warn(XmlGaugeImporter::tr("an uneven scale running backwards is drawn even"));
                    }
            }
        return m;
    }

    GaugeComponent shape(GaugeComponent::ShapeKind kind, const QRectF &frame, const QColor &c, double lineWidth = 0.0)
    {
        GaugeComponent s;
        s.type = GaugeComponent::Shape;
        s.shape = kind;
        s.frame = frame;
        s.color = custom(c);
        s.filled = lineWidth <= 0.0 && kind != GaugeComponent::Arc && kind != GaugeComponent::Line;
        s.lineWidth = lineWidth > 0.0 ? lineWidth : 1.0;
        return s;
    }

    GaugeComponent line(const QPointF &a, const QPointF &b, const QColor &c, double width)
    {
        GaugeComponent s = shape(GaugeComponent::Line, QRectF(a, b).normalized(), c, width);
        s.points = {a, b};
        return s;
    }

    static QRectF circle(const QPointF &centre, double rx, double ry)
    {
        return QRectF(centre.x() - rx, centre.y() - ry, 2.0 * rx, 2.0 * ry);
    }

    GaugeComponent image(const Element &e, const QRectF &frame)
    {
        GaugeComponent c;
        c.type = GaugeComponent::Image;
        c.frame = frame;
        const QString src = e.string("src");
        if (src.isEmpty())
            {
                return c;
            }
        c.imagePath = "images/" + QFileInfo(QDir::fromNativeSeparators(src)).fileName();
        if (m_imageFolder.isEmpty())
            {
                return c;
            }
        const QString path = findFile(m_imageFolder, src);
        QImage loaded = path.isEmpty() ? QImage() : QImage(path);
        if (loaded.isNull())
            {
                warn(XmlGaugeImporter::tr("image %1 not found").arg(src));
                return c;
            }
        if (c.frame.width() <= 0.0 || c.frame.height() <= 0.0)
            {
                c.frame.setSize(loaded.size());
            }
        // the picture's own transparency goes into its pixels
        const double opacity = std::clamp(e.number("transparency", 1.0) * m_opacity, 0.0, 1.0);
        if (opacity < 1.0)
            {
                QImage faded(loaded.size(), QImage::Format_ARGB32_Premultiplied);
                faded.fill(Qt::transparent);
                QPainter painter(&faded);
                painter.setOpacity(opacity);
                painter.drawImage(0, 0, loaded);
                painter.end();
                loaded = faded;
            }
        c.image = loaded;
        return c;
    }

    GaugeTicks ticks(const Element &e, const QString &prefix)
    {
        GaugeTicks t;
        t.unit = e.number(prefix + "TickUnit");
        t.length = e.number(prefix + "TickLength");
        t.width = penWidth(e, prefix + "Tick");
        t.color = custom(color(e, prefix + "Tick"));
        return t;
    }

    void convert(const Element &e, const QPointF &parentOrigin, const Element *face, QVector<GaugeComponent> *out)
    {
        const QPointF origin = parentOrigin + QPointF(e.number("x"), e.number("y"));
        const QString &type = e.name;
        if (type == "Image")
            {
                out->append(image(e, QRectF(origin, QSizeF(e.number("width", -1.0), e.number("height", -1.0)))));
            }
        else if (type == "ImageNeedle" || type == "NonLinearImageNeedle")
            {
                const QPointF axis(e.number("axisX"), e.number("axisY"));
                GaugeComponent c = image(e, QRectF(origin - axis, QSizeF(e.number("width", -1.0), e.number("height", -1.0))));
                double minAngle = 0.0;
                double maxAngle = 0.0;
                GaugeManipulation m = range(e, face, &minAngle, &maxAngle);
                m.type = GaugeManipulation::Rotate;
                m.pivot = origin;
                // the picture is turned by these angles from how it is drawn
                m.minAngle = minAngle;
                m.maxAngle = maxAngle;
                c.manipulations.append(m);
                applyInput(e, "Value", &c);
                out->append(c);
            }
        else if (type == "Needle" || type == "NonLinearNeedle")
            {
                // drawn pointing up, then turned: angles here count from 3 o'clock
                const double offset = e.number("offset");
                const double length = e.number("length", 50.0);
                GaugeComponent c = line(origin - QPointF(0.0, offset), origin - QPointF(0.0, length), color(e, QString()),
                                        penWidth(e, QString()));
                double minAngle = 0.0;
                double maxAngle = 0.0;
                GaugeManipulation m = range(e, face, &minAngle, &maxAngle);
                m.type = GaugeManipulation::Rotate;
                m.pivot = origin;
                m.minAngle = minAngle + 90.0;
                m.maxAngle = maxAngle + 90.0;
                c.manipulations.append(m);
                applyInput(e, "Value", &c);
                out->append(c);
            }
        else if (type == "AnimatedArc" || type == "NonLinearAnimatedArc" || type == "AnimatedPie" || type == "NonLinearAnimatedPie")
            {
                const bool pie = type.endsWith("Pie");
                const double radius = e.number("radius", 50.0);
                double minAngle = 0.0;
                double maxAngle = 0.0;
                GaugeManipulation m = range(e, face, &minAngle, &maxAngle);
                GaugeComponent c = shape(pie ? GaugeComponent::Pie : GaugeComponent::Arc, circle(origin, radius, radius),
                                         color(e, QString()), pie ? 0.0 : penWidth(e, QString()));
                c.startAngle = minAngle + 90.0;
                c.sweepAngle = maxAngle - minAngle;
                // as much of it shows as the value has reached
                m.type = GaugeManipulation::RadialClip;
                m.pivot = origin;
                m.minAngle = minAngle + 90.0;
                m.maxAngle = maxAngle + 90.0;
                c.manipulations.append(m);
                applyInput(e, "Value", &c);
                out->append(c);
            }
        else if (type == "Arc" || type == "Pie")
            {
                const double radius = e.number("radius", 50.0);
                const bool pie = type == "Pie";
                GaugeComponent c = shape(pie ? GaugeComponent::Pie : GaugeComponent::Arc, circle(origin, radius, radius),
                                         color(e, QString()), pie ? 0.0 : penWidth(e, QString()));
                c.startAngle = e.number("minAngle") + 90.0;
                c.sweepAngle = e.number("maxAngle") - e.number("minAngle");
                out->append(c);
            }
        else if (type == "Ellipse" || type == "FilledEllipse")
            {
                out->append(shape(GaugeComponent::Ellipse, circle(origin, e.number("horizontalRadius", 10.0), e.number("verticalRadius", 10.0)),
                                  color(e, QString()), type == "Ellipse" ? penWidth(e, QString()) : 0.0));
            }
        else if (type == "Rectangle" || type == "FilledRectangle")
            {
                GaugeComponent c = shape(GaugeComponent::Rectangle, QRectF(origin, QSizeF(e.number("width", 50.0), e.number("height", 50.0))),
                                         color(e, QString()), type == "Rectangle" ? penWidth(e, QString()) : 0.0);
                if (e.flag("roundCorners"))
                    {
                        c.cornerRadius = e.number("cornerRadius", 5.0);
                    }
                out->append(c);
            }
        else if (type == "Bar")
            {
                convertBar(e, origin, out);
            }
        else if (type == "Text" || type == "DynamicText")
            {
                convertText(e, origin, out);
            }
        else if (type == "AnalogFace")
            {
                convertFace(e, origin, out);
                convertAll(e, origin, &e, out);
                return;
            }
        else if (type == "TickerTape")
            {
                convertTape(e, origin, out);
            }
        else if (type == "GPlot")
            {
                convertGPlot(e, origin, out);
            }
        else if (type == "Map")
            {
                convertMap(e, origin, out);
                return;
            }
        else if (type == "ChartComponent")
            {
                convertChart(e, origin, out);
            }
        else
            {
                warn(XmlGaugeImporter::tr("%1 is not supported").arg(type));
            }
        // what lies inside a part is drawn after it, from its origin
        convertAll(e, origin, face, out);
    }

    void convertBar(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        const double width = e.number("width", 50.0);
        const double height = e.number("height", 10.0);
        GaugeComponent c = shape(GaugeComponent::Rectangle, QRectF(origin, QSizeF(width, height)), color(e, "start"));
        c.gradientColor = custom(color(e, "end"));
        GaugeManipulation m;
        m.hasValueRange = true;
        m.minValue = e.number("minValue");
        m.maxValue = e.number("maxValue", 100.0);
        // the colours run along the whole bar; the value uncovers part of it
        const QString direction = e.string("direction");
        const double left = origin.x();
        const double top = origin.y();
        if (direction == "RightToLeft")
            {
                c.gradientLine = QLineF(left + width, top, left - 1.0, top);
                m.type = GaugeManipulation::WidthClip;
                m.minX = left + width;
                m.maxX = left;
            }
        else if (direction == "TopToBottom")
            {
                c.gradientLine = QLineF(left, top - 1.0, left, top + height);
                m.type = GaugeManipulation::HeightClip;
                m.minY = top;
                m.maxY = top + height;
            }
        else if (direction == "BottomToTop")
            {
                c.gradientLine = QLineF(left, top + height, left, top - 1.0);
                m.type = GaugeManipulation::HeightClip;
                m.minY = top + height;
                m.maxY = top;
            }
        else
            {
                c.gradientLine = QLineF(left - 1.0, top, left + width, top);
                m.type = GaugeManipulation::WidthClip;
                m.minX = left;
                m.maxX = left + width;
            }
        c.manipulations.append(m);
        applyInput(e, "Value", &c);
        out->append(c);
    }

    void convertText(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        GaugeComponent c;
        c.type = GaugeComponent::Text;
        c.boxed = true;
        c.frame = QRectF(origin, QSizeF(e.number("fontRectWidth", 50.0), e.number("fontRectHeight", 25.0)));
        applyText(e, QString(), true, &c);
        if (e.name == "Text")
            {
                c.text = e.string("text");
                out->append(c);
                return;
            }
        c.autoText = GaugeComponent::Value;
        c.precision = int(e.number("decimalPrecision", 2));
        c.showPlus = e.flag("showPlus");
        if (e.flag("restrictRange"))
            {
                c.clampValue = true;
                c.clampMin = e.number("restrictedMinValue");
                c.clampMax = e.number("restrictedMaxValue");
            }
        const QString display = e.string("displayType", "Decimal");
        if (display == "Integer")
            {
                c.valueFormat = GaugeComponent::WholeNumber;
            }
        else if (display == "Running Time" || display == "Time")
            {
                c.valueFormat = GaugeComponent::Duration;
            }
        else if (display == "Degrees, Minutes, Seconds")
            {
                c.valueFormat = GaugeComponent::DegreesMinutesSeconds;
            }
        else if (display.startsWith("Time:") || display.startsWith("Date:"))
            {
                c.valueFormat = GaugeComponent::Clock;
                c.clockFormat = clockFormat(display);
                c.localTime = e.flag("convertFromGMT", true);
            }
        applyInput(e, "Value", &c);
        out->append(c);
    }

    // .NET date and time patterns -> QDateTime ones
    static QString clockFormat(const QString &display)
    {
        const QLocale locale = QLocale::system();
        if (display == "Time: Short")
            {
                return locale.timeFormat(QLocale::ShortFormat);
            }
        if (display == "Time: Long")
            {
                return locale.timeFormat(QLocale::LongFormat);
            }
        if (display == "Date: Short")
            {
                return locale.dateFormat(QLocale::ShortFormat);
            }
        if (display == "Date: Long")
            {
                return locale.dateFormat(QLocale::LongFormat);
            }
        QString format = display.mid(display.indexOf(':') + 1).trimmed();
        static const QRegularExpression fractions("f+");
        format.replace(fractions, "zzz");
        format.replace("tt", "AP");
        return format;
    }

    void convertFace(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        GaugeComponent c;
        c.type = GaugeComponent::Dial;
        GaugeScale &s = c.scale;
        s.radius = e.number("arcRadius", 50.0);
        c.frame = circle(origin, s.radius, s.radius);
        s.minValue = e.number("minValue");
        s.maxValue = e.number("maxValue", 100.0);
        s.startAngle = e.number("minAngle") + 90.0;
        s.endAngle = e.number("maxAngle") + 90.0;
        s.arcWidth = penWidth(e, "arc");
        s.arcColor = custom(color(e, "arc"));
        s.minor = ticks(e, "minor");
        s.major = ticks(e, "major");
        s.labelOffset = e.number("labelOffset");
        applyText(e, QString(), false, &c);
        out->append(c);
    }

    void convertTape(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        GaugeComponent c;
        c.type = GaugeComponent::Tape;
        GaugeScale &s = c.scale;
        const QString orientation = e.string("orientation");
        s.orientation = orientation.contains("Bottom") ? GaugeScale::NumbersBelow
                        : orientation.contains("Left") ? GaugeScale::NumbersLeft
                        : orientation.contains("Right") ? GaugeScale::NumbersRight : GaugeScale::NumbersAbove;
        s.reverse = e.flag("reverse");
        s.minValue = e.number("minValue", -100.0);
        s.maxValue = e.number("maxValue", 100.0);
        s.loop = e.flag("loop");
        s.length = e.number("length", 200.0);
        s.spacing = e.number("spacing", 25.0);
        s.minor = ticks(e, "minor");
        s.major = ticks(e, "major");
        s.labelOffset = e.number("labelOffset", 10.0);
        s.markerLength = e.number("valueLineLength");
        s.markerWidth = penWidth(e, "valueLine");
        s.markerColor = custom(color(e, "valueLine"));
        applyText(e, QString(), false, &c);
        const double thickness = s.major.length + s.labelOffset + c.fontSize * 1.5;
        const bool horizontal = s.orientation == GaugeScale::NumbersAbove || s.orientation == GaugeScale::NumbersBelow;
        c.frame = QRectF(origin, horizontal ? QSizeF(s.length, thickness) : QSizeF(thickness, s.length));
        applyInput(e, "Value", &c);
        out->append(c);
    }

    void convertGPlot(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        const double radius = e.number("radius", 50.0);
        const double maxG = e.number("maxG", 2.0);
        const QColor axes = color(e, "axes");
        const double axesWidth = penWidth(e, "axes");
        out->append(line(origin - QPointF(radius, 0.0), origin + QPointF(radius, 0.0), axes, axesWidth));
        out->append(line(origin - QPointF(0.0, radius), origin + QPointF(0.0, radius), axes, axesWidth));
        if (maxG > 0.0)
            {
                const double major = e.number("majorUnit", 1.0);
                const double minor = e.number("minorUnit", 0.5);
                if (major > 0.0)
                    {
                        const int count = int(std::floor(maxG / major + 1e-9));
                        for (int i = 1; i <= std::min(count, 100); ++i)
                            {
                                const double r = radius * i * major / maxG;
                                out->append(shape(GaugeComponent::Ellipse, circle(origin, r, r), color(e, "major"), penWidth(e, "major")));
                                GaugeComponent label;
                                label.type = GaugeComponent::Text;
                                label.text = scaleNumber(i * major);
                                label.frame = QRectF(origin + QPointF(r, 0.0), QSizeF());
                                applyText(e, QString(), false, &label);
                                out->append(label);
                            }
                    }
                if (minor > 0.0)
                    {
                        const int count = int(std::floor(maxG / minor + 1e-9));
                        for (int i = 1; i <= std::min(count, 100); ++i)
                            {
                                const double g = i * minor;
                                if (major > 0.0 && std::fabs(g / major - std::round(g / major)) < 1e-9)
                                    {
                                        continue;
                                    }
                                const double r = radius * g / maxG;
                                out->append(shape(GaugeComponent::Ellipse, circle(origin, r, r), color(e, "minor"), penWidth(e, "minor")));
                            }
                    }
            }
        const double ballRadius = e.number("ballRadius", 3.0);
        GaugeComponent ball = shape(GaugeComponent::Ellipse, circle(origin, ballRadius, ballRadius), color(e, "ball"));
        GaugeManipulation m;
        m.type = GaugeManipulation::Translate2D;
        m.maxRadius = radius;
        m.hasValueRange = true;
        m.minValue = -maxG;
        m.maxValue = maxG;
        QString unit;
        m.xChannel = channelOf(e, "Lateral G", &unit);
        m.yChannel = channelOf(e, "Acceleration G", &unit);
        // more acceleration moves the ball down here, and up in ours
        m.invertX = e.flag("flipXAxis");
        m.invertY = !e.flag("flipYAxis");
        ball.manipulations.append(m);
        out->append(ball);
        warn(XmlGaugeImporter::tr("the G-plot's stick and tail are not drawn"));
    }

    void convertMap(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        GaugeComponent c;
        c.type = GaugeComponent::TrackMap;
        c.frame = QRectF(origin, QSizeF(e.number("width", 100.0), e.number("height", 100.0)));
        c.color = custom(color(e, "Even"));
        c.lineWidth = penWidth(e, "Even");
        // a position dot or picture moves along the route; a map inside a map
        // is another line under or over it (a shadow, an outline)
        QVector<const Element *> inner;
        if (const Element *components = e.child("Components"))
            {
                for (const Element &part : components->children)
                    {
                        const QPointF offset(part.number("x"), part.number("y"));
                        if (part.name == "MapDot")
                            {
                                const double r = part.number("radius", 2.0);
                                c.knob.append(shape(GaugeComponent::Ellipse, circle(offset, r, r), color(part, QString())));
                                QString unit;
                                channelOf(part, "X", &unit);   // warns about a ghost dot
                            }
                        else if (part.name == "MapPositionImage")
                            {
                                const QSizeF size(part.number("width", -1.0), part.number("height", -1.0));
                                GaugeComponent picture = image(part, QRectF(offset, size));
                                picture.frame.moveCenter(offset);
                                // turned to the direction of travel
                                GaugeManipulation turn;
                                turn.type = GaugeManipulation::Rotate;
                                turn.pivot = offset;
                                turn.hasValueRange = true;
                                turn.minValue = 0.0;
                                turn.maxValue = 360.0;
                                turn.minAngle = 0.0;
                                turn.maxAngle = 360.0;
                                turn.wrapValue = true;
                                picture.manipulations.append(turn);
                                QString unit;
                                picture.channel = channelOf(part, "Heading", &unit);
                                if (picture.channel.isEmpty())
                                    {
                                        picture.channel = ChannelId::Heading;
                                    }
                                c.knob.append(picture);
                            }
                        else if (part.name == "Map")
                            {
                                inner.append(&part);
                            }
                        else
                            {
                                warn(XmlGaugeImporter::tr("%1 is not supported").arg(part.name));
                            }
                    }
            }
        // follows the position, whatever the gauge is bound to
        c.channel = ChannelId::PositionLat;
        out->append(c);
        for (const Element *map : inner)
            {
                convertMap(*map, origin + QPointF(map->number("x"), map->number("y")), out);
            }
    }

    void convertChart(const Element &e, const QPointF &origin, QVector<GaugeComponent> *out)
    {
        const QRectF frame(origin, QSizeF(e.number("width", 200.0), e.number("height", 100.0)));
        const QColor background = color(e, "background");
        if (background.alpha() > 0)
            {
                out->append(shape(GaugeComponent::Rectangle, frame, background));
            }
        if (e.flag("showAxisLines", true))
            {
                const QColor axis = color(e, "axis");
                const double width = penWidth(e, "axis");
                const double x = e.flag("yAxisOnRight") ? frame.right() : frame.left();
                out->append(line(QPointF(x, frame.top()), QPointF(x, frame.bottom()), axis, width));
                out->append(line(frame.bottomLeft(), frame.bottomRight(), axis, width));
            }
        GaugeComponent c;
        c.type = GaugeComponent::Graph;
        c.frame = frame;
        c.color = custom(color(e, "line"));
        c.lineWidth = penWidth(e, "line");
        if (e.flag("fillChart"))
            {
                const QColor fill = color(e, "fill");
                c.fillAlpha = fill.alpha();
                c.fillColor = QColor(fill.red(), fill.green(), fill.blue());
            }
        const bool byTime = e.string("xAxisType", "Time (Seconds)").startsWith("Time");
        if (!byTime)
            {
                warn(XmlGaugeImporter::tr("a chart over distance is drawn over time"));
            }
        if (!e.flag("xScaleAuto", true) && byTime)
            {
                c.windowBefore = std::max(0.0, e.number("xUnitsBackward", 60.0));
                c.windowAfter = std::max(0.0, e.number("xUnitsForward", 10.0));
            }
        if (!e.flag("yScaleAuto", true))
            {
                warn(XmlGaugeImporter::tr("a chart's fixed value scale is set from the data"));
            }
        if (e.flag("xAxisShowLabel") || e.flag("yAxisShowLabel") || e.flag("xAxisShowUnits") || e.flag("yAxisShowUnits"))
            {
                warn(XmlGaugeImporter::tr("chart axis labels are not drawn"));
            }
        if (e.flag("showCurrentLine"))
            {
                c.xMarker.append(line(QPointF(0.0, 0.0), QPointF(0.0, frame.height()), color(e, "currentLine"),
                                      penWidth(e, "currentLine")));
            }
        const double dot = e.number("currentDotRadius");
        if (dot > 0.0)
            {
                c.knob.append(shape(GaugeComponent::Ellipse, circle(QPointF(), dot, dot), color(e, "currentDot")));
            }
        applyInput(e, "Value", &c);
        out->append(c);
    }
};

// Version 2 named some data types without their unit; it was always the same one.
QString withVersion2Unit(const QString &category, const QString &type)
{
    if (category == "Positional")
        {
            static const QStringList inFeet{"Longitude / X Position", "Latitude / Y Position", "Altitude / Z Position", "Distance"};
            return inFeet.contains(type) ? type + " (Feet)" : type;
        }
    if (category == "Velocity")
        {
            return type.endsWith(')') ? type : type + " (MPH)";
        }
    if (type.endsWith("Temperature"))
        {
            return type + " (F)";
        }
    if (type.endsWith("Pressure"))
        {
            return type + " (psi)";
        }
    return type;
}

}

QString XmlGaugeImporter::channelFor(const QString &dataType, QString *unit)
{
    return DataTypes::channelFor(dataType, unit);
}

XmlGaugeImporter::Result XmlGaugeImporter::parse(const QByteArray &xml, const QString &name, const QString &imageFolder)
{
    Result result;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd() && !reader.isStartElement())
        {
            reader.readNext();
        }
    Element root;
    if (!reader.isStartElement() || !readElement(reader, &root))
        {
            result.error = tr("This is not an XML gauge: %1").arg(reader.errorString());
            return result;
        }
    if (root.name != "Gauge")
        {
            result.error = tr("This is not an XML gauge.");
            return result;
        }
    const int version = int(root.number("version", 3));
    if (version != 2 && version != 3)
        {
            result.error = tr("Gauge file version %1 is not supported.").arg(root.string("version"));
            return result;
        }
    Gauge &gauge = result.gauge;
    gauge.name = name;
    gauge.size = QSizeF(root.number("width"), root.number("height"));
    if (gauge.size.isEmpty())
        {
            result.error = tr("The gauge has no size.");
            return result;
        }

    Converter converter(std::clamp(root.number("transparency", 1.0), 0.0, 1.0), imageFolder);
    if (const Element *inputs = root.child("Inputs"))
        {
            QString first;
            for (const Element &input : inputs->children)
                {
                    const QString type = version == 2 ? withVersion2Unit(input.string("category"), input.string("name"))
                                         : input.string("name");
                    converter.inputTypes.insert(input.text, type);
                    first = first.isEmpty() ? type : first;
                }
            // the first one is what the gauge is about
            converter.mainChannel = channelFor(first, &converter.mainUnit);
        }
    if (!converter.mainChannel.isEmpty())
        {
            gauge.supportedTypes << converter.mainChannel;
        }
    converter.convertAll(root, QPointF(), nullptr, &gauge.components);
    if (gauge.components.isEmpty())
        {
            result.error = tr("The gauge has nothing in it that can be shown.");
            return result;
        }
    result.warnings = converter.warnings;
    result.ok = true;
    return result;
}

XmlGaugeImporter::Result XmlGaugeImporter::importFolder(const QString &folder)
{
    const QString path = findFile(folder, FileName);
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        {
            Result result;
            result.error = tr("There is no %1 in %2.").arg(FileName, folder);
            return result;
        }
    return parse(file.readAll(), QFileInfo(folder).fileName(), folder);
}
