#include "xmldataprofileimporter.h"

#include "../datatypes.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

#include <cmath>

namespace
{
struct Element
{
    QString name;
    QHash<QString, QString> attributes;
    QVector<Element> children;
    QString text;    // as it is: separators may be white space

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
    QStringList texts(const QString &listName) const
    {
        QStringList result;
        if (const Element *list = child(listName))
            {
                for (const Element &item : list->children)
                    {
                        result << item.text.trimmed();
                    }
            }
        return result;
    }
    bool flag(const QString &key) const { return attributes.value(key).compare("true", Qt::CaseInsensitive) == 0; }
    double number(const QString &key, double fallback = 0.0) const
    {
        bool ok = false;
        const double value = attributes.value(key).toDouble(&ok);
        return ok ? value : fallback;
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
                    return true;
                }
        }
    return !reader.hasError();
}

// A unit as a linear step to a base one: base = value * scale + offset.
struct Linear
{
    double scale{1.0};
    double offset{0.0};
};

bool toBase(const QString &unit, Linear *linear)
{
    static const QHash<QString, Linear> units
    {
        // metres
        {"Meters", {1.0, 0.0}}, {"Feet", {0.3048, 0.0}}, {"Miles", {1609.344, 0.0}}, {"Kilometers", {1000.0, 0.0}},
        // metres per second
        {"m/s", {1.0, 0.0}}, {"MPH", {0.44704, 0.0}}, {"KPH", {1.0 / 3.6, 0.0}}, {"Knots", {1852.0 / 3600.0, 0.0}},
        {"FPM", {0.3048 / 60.0, 0.0}},
        // degrees Celsius
        {"Celsius", {1.0, 0.0}}, {"Fahrenheit", {5.0 / 9.0, -32.0 * 5.0 / 9.0}}, {"Kelvin", {1.0, -273.15}},
        // kPa
        {"kPa", {1.0, 0.0}}, {"psi", {6.894757, 0.0}}, {"bar", {100.0, 0.0}},
    };
    const auto found = units.constFind(unit);
    if (found == units.constEnd())
        {
            return false;
        }
    *linear = found.value();
    return true;
}

const char *const kDerived[] =
{
    "LapSummaryCalculator", "AscentDescentCalculator", "HeadingCalculator", "DistanceTravelledCalculator",
    "GPSAccelerationCalculator", "DistanceToSpeedCalculator",
};
}

bool XmlDataProfileImporter::isDataProfile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            return false;
        }
    QXmlStreamReader reader(file.read(4096));
    while (!reader.atEnd())
        {
            if (reader.readNext() == QXmlStreamReader::StartElement)
                {
                    return reader.name() == QLatin1String("DataProfile");
                }
        }
    return false;
}

XmlDataProfileImporter::Result XmlDataProfileImporter::importFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            Result result;
            result.error = tr("Cannot open %1: %2").arg(path, file.errorString());
            return result;
        }
    return parse(file.readAll(), QFileInfo(path).completeBaseName());
}

XmlDataProfileImporter::Result XmlDataProfileImporter::parse(const QByteArray &xml, const QString &name)
{
    Result result;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd() && !reader.isStartElement())
        {
            reader.readNext();
        }
    Element root;
    if (!reader.isStartElement() || !readElement(reader, &root) || root.name != "DataProfile")
        {
            result.error = tr("This is not an XML data profile.");
            return result;
        }
    if (root.flag("isAimData") || root.flag("isDriftBoxData"))
        {
            result.error = tr("Files of this logger are laid out in a way that is not supported yet.");
            return result;
        }
    DataProfile &p = result.profile;
    p.name = name;
    p.headerLines.clear();
    for (const QString &line : root.texts("HeaderLines"))
        {
            p.headerLines << line.toInt();
        }
    if (p.headerLines.isEmpty())
        {
            p.headerLines << 0;
        }
    if (const Element *start = root.child("DataStartLineIndex"))
        {
            p.dataStart = start->text.trimmed().toInt();
        }
    if (const Element *separators = root.child("DataValueSeparators"))
        {
            // white space counts here: a tab is a separator
            p.separators = separators->text;
            p.separators.remove('\n');
            p.separators.remove('\r');
        }
    if (p.separators.isEmpty())
        {
            p.separators = ",";
        }
    if (const Element *euro = root.child("UseEuroDecimalFormat"))
        {
            p.decimalComma = euro->text.trimmed().compare("true", Qt::CaseInsensitive) == 0;
        }
    if (const Element *matchers = root.child("FileLineMatchers"))
        {
            for (const Element &m : matchers->children)
                {
                    p.matchers.append({int(m.number("dataLineIndex")), m.attributes.value("matchText"),
                                       m.attributes.value("matchMode") != "ExactMatch", m.flag("ignoreCase")});
                }
        }

    QStringList derived;
    if (const Element *calculators = root.child("Calculators"))
        {
            for (const Element &e : calculators->children)
                {
                    DataProfile::Calculator c;
                    c.inputs = e.texts("InputColumns");
                    c.outputs = e.texts("OutputColumns");
                    const QString type = e.name;
                    const QString from = e.attributes.value("inputUnit");
                    const QString to = e.attributes.value("outputUnit");
                    if (type == "DistanceConverter" || type == "SpeedConverter" || type == "TemperatureConverter"
                            || type == "PressureConverter")
                        {
                            Linear in;
                            Linear out;
                            if (!toBase(from, &in) || !toBase(to, &out))
                                {
                                    result.warnings << tr("unknown units %1, %2").arg(from, to);
                                    continue;
                                }
                            c.type = DataProfile::Calculator::Convert;
                            c.scale = in.scale / out.scale;
                            c.offset = (in.offset - out.offset) / out.scale;
                            if (type == "PressureConverter" && e.flag("makeRelativeToAtmosphere"))
                                {
                                    c.offset -= 101.325 / out.scale;
                                }
                        }
                    else if (type == "MathCalculator")
                        {
                            static const QHash<QString, QString> operations
                            {
                                {"Add", "add"}, {"Subtract", "subtract"}, {"Multiply", "multiply"}, {"Divide", "divide"},
                                {"SquareRoot", "sqrt"}, {"Exponent", "power"}, {"Modulus", "modulus"}, {"Log", "log"},
                                {"Pythagorean", "hypot"}, {"Sine", "sin"}, {"Cosine", "cos"}, {"Tangent", "tan"},
                                {"ArcSine", "asin"}, {"ArcCosine", "acos"}, {"ArcTangent", "atan"}, {"Inverse", "inverse"},
                            };
                            c.type = DataProfile::Calculator::Math;
                            c.operation = operations.value(e.attributes.value("mathOperation"));
                            if (c.operation.isEmpty())
                                {
                                    result.warnings << tr("unknown operation %1").arg(e.attributes.value("mathOperation"));
                                    continue;
                                }
                            c.constant = e.flag("useConstantParameter");
                            c.parameter = e.number("parameter");
                        }
                    else if (type == "AngleConverter")
                        {
                            static const QHash<QString, QString> units{{"Degrees", "degrees"}, {"Radians", "radians"},
                                {"Grade Percentage", "grade"}};
                            const QString a = units.value(from);
                            const QString b = units.value(to);
                            if (a.isEmpty() || b.isEmpty())
                                {
                                    result.warnings << tr("unknown units %1, %2").arg(from, to);
                                    continue;
                                }
                            c.type = DataProfile::Calculator::Angle;
                            c.operation = a + "To" + b.left(1).toUpper() + b.mid(1);
                        }
                    else if (type == "LinearInterpolator")
                        {
                            c.type = DataProfile::Calculator::Fill;
                            c.deviations = e.flag("filterValues") ? e.number("standardDevCeiling", 5.0) : 0.0;
                        }
                    else if (type == "AveragingCalculator")
                        {
                            const QString direction = e.attributes.value("direction");
                            c.type = DataProfile::Calculator::Average;
                            c.window = e.number("parameter");
                            c.angle = e.attributes.value("mode") == "Angle";
                            c.before = direction != "Forwards";
                            c.after = direction != "Backwards";
                        }
                    else if (type == "DeltaValueCalculator")
                        {
                            c.type = DataProfile::Calculator::Delta;
                        }
                    else if (type == "UsePreviousValueCalculator")
                        {
                            c.type = DataProfile::Calculator::Previous;
                        }
                    else if (type == "PpmToRpmConverter")
                        {
                            // two pulses a turn per cylinder pair
                            c.type = DataProfile::Calculator::Convert;
                            c.scale = 2.0 / std::max(1.0, e.number("cylinderCount", 4.0));
                            if (e.flag("promptForCylinderCount"))
                                {
                                    result.warnings << tr("RPM is worked out for %n cylinder(s)", nullptr, int(e.number("cylinderCount", 4.0)));
                                }
                        }
                    else if (type == "DegreesToCartesianConverter")
                        {
                            continue;   // positions stay in degrees here
                        }
                    else
                        {
                            bool known = false;
                            for (const char *d : kDerived)
                                {
                                    known = known || type == QLatin1String(d);
                                }
                            if (!known)
                                {
                                    result.warnings << tr("%1 is not supported").arg(type);
                                }
                            else if (!derived.contains(type))
                                {
                                    derived << type;
                                }
                            continue;
                        }
                    p.calculators.append(c);
                }
        }

    if (const Element *mappings = root.child("ColumnMappings"))
        {
            for (const Element &e : mappings->children)
                {
                    DataProfile::Mapping m;
                    m.column = e.attributes.value("inputColumnText");
                    m.contains = e.attributes.value("inputColumnMatchMode") == "Contains";
                    m.ignoreCase = e.flag("inputColumnIgnoreCase");
                    const QString type = e.attributes.value("dataType");
                    if (type == "Data Running Time, Seconds")
                        {
                            m.role = DataProfile::Mapping::RunningTime;
                        }
                    else if (type == "Time of Day")
                        {
                            m.role = DataProfile::Mapping::TimeOfDay;
                        }
                    else if (type == "Date")
                        {
                            m.role = DataProfile::Mapping::Date;
                        }
                    else if (type == "Lap Indicator")
                        {
                            m.role = DataProfile::Mapping::Lap;
                        }
                    else if (type == "Lap Running Time"
                             || ((type.startsWith("Longitude / X Position") || type.startsWith("Latitude / Y Position"))
                                 && !type.endsWith("(Degrees)")))
                        {
                            continue;   // worked out from the laps; projected positions
                        }
                    else
                        {
                            m.channel = DataTypes::channelFor(type, &m.unit);
                        }
                    if (!m.column.isEmpty())
                        {
                            p.mappings.append(m);
                        }
                }
        }
    bool hasTime = false;
    for (const DataProfile::Mapping &m : p.mappings)
        {
            hasTime = hasTime || m.role == DataProfile::Mapping::RunningTime;
        }
    if (!hasTime)
        {
            result.error = tr("The data profile has no time column.");
            return result;
        }
    if (p.matchers.isEmpty())
        {
            result.error = tr("The data profile does not say how to recognise its files.");
            return result;
        }
    if (!derived.isEmpty())
        {
            result.warnings.prepend(tr("worked out later: %1").arg(derived.join(", ")));
        }
    result.ok = true;
    return result;
}
