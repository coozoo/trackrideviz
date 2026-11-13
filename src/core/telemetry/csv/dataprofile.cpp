#include "dataprofile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace
{
const char kFormat[] = "rtvdataprofile";
const int kVersion = 1;

const char *const kCalculatorTypes[] = {"convert", "math", "angle", "fill", "average", "delta", "previous"};
const char *const kRoles[] = {"value", "runningTime", "timeOfDay", "date", "lap"};

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

QJsonArray strings(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList strings(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray())
        {
            list << item.toString();
        }
    return list;
}

QJsonObject calculatorToJson(const DataProfile::Calculator &c)
{
    QJsonObject object{{"type", kCalculatorTypes[c.type]}, {"inputs", strings(c.inputs)}, {"outputs", strings(c.outputs)}};
    switch (c.type)
        {
        case DataProfile::Calculator::Convert:
            object["scale"] = c.scale;
            object["offset"] = c.offset;
            break;
        case DataProfile::Calculator::Math:
            object["operation"] = c.operation;
            if (c.constant)
                {
                    object["parameter"] = c.parameter;
                }
            break;
        case DataProfile::Calculator::Angle:
            object["operation"] = c.operation;
            break;
        case DataProfile::Calculator::Fill:
            if (c.deviations > 0.0)
                {
                    object["deviations"] = c.deviations;
                }
            break;
        case DataProfile::Calculator::Average:
            object["window"] = c.window;
            object["before"] = c.before;
            object["after"] = c.after;
            if (c.angle)
                {
                    object["angle"] = true;
                }
            break;
        default:
            break;
        }
    return object;
}

DataProfile::Calculator calculatorFromJson(const QJsonObject &object)
{
    DataProfile::Calculator c;
    c.type = DataProfile::Calculator::Type(indexOf(kCalculatorTypes, object.value("type").toString()));
    c.inputs = strings(object.value("inputs"));
    c.outputs = strings(object.value("outputs"));
    c.scale = object.value("scale").toDouble(1.0);
    c.offset = object.value("offset").toDouble();
    c.operation = object.value("operation").toString();
    c.constant = c.type != DataProfile::Calculator::Math || object.contains("parameter");
    c.parameter = object.value("parameter").toDouble();
    c.deviations = object.value("deviations").toDouble();
    c.window = object.value("window").toDouble();
    c.before = object.value("before").toBool(true);
    c.after = object.value("after").toBool(true);
    c.angle = object.value("angle").toBool();
    return c;
}
}

bool DataProfile::Calculator::operator==(const Calculator &other) const
{
    return type == other.type && inputs == other.inputs && outputs == other.outputs && scale == other.scale
           && offset == other.offset && operation == other.operation && constant == other.constant
           && parameter == other.parameter && deviations == other.deviations && window == other.window
           && before == other.before && after == other.after && angle == other.angle;
}

bool DataProfile::matches(const QStringList &lines) const
{
    if (matchers.isEmpty())
        {
            return false;
        }
    for (const LineMatcher &m : matchers)
        {
            if (m.line < 0 || m.line >= lines.size())
                {
                    return false;
                }
            const Qt::CaseSensitivity cs = m.ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
            const QString line = lines.at(m.line).trimmed();
            const bool found = m.contains ? line.contains(m.text, cs) : line.compare(m.text.trimmed(), cs) == 0;
            if (!found)
                {
                    return false;
                }
        }
    return true;
}

int DataProfile::linesToMatch() const
{
    int count = 0;
    for (const LineMatcher &m : matchers)
        {
            count = std::max(count, m.line + 1);
        }
    return count;
}

QByteArray DataProfileFile::toJson(const DataProfile &profile)
{
    QJsonArray matchers;
    for (const DataProfile::LineMatcher &m : profile.matchers)
        {
            QJsonObject object{{"line", m.line}, {"text", m.text}};
            if (!m.contains)
                {
                    object["wholeLine"] = true;
                }
            if (m.ignoreCase)
                {
                    object["ignoreCase"] = true;
                }
            matchers.append(object);
        }
    QJsonArray headers;
    for (const int line : profile.headerLines)
        {
            headers.append(line);
        }
    QJsonArray calculators;
    for (const DataProfile::Calculator &c : profile.calculators)
        {
            calculators.append(calculatorToJson(c));
        }
    QJsonArray mappings;
    for (const DataProfile::Mapping &m : profile.mappings)
        {
            QJsonObject object{{"column", m.column}, {"role", kRoles[m.role]}};
            if (m.contains)
                {
                    object["contains"] = true;
                }
            if (m.ignoreCase)
                {
                    object["ignoreCase"] = true;
                }
            if (!m.channel.isEmpty())
                {
                    object["channel"] = m.channel;
                }
            if (!m.unit.isEmpty())
                {
                    object["unit"] = m.unit;
                }
            mappings.append(object);
        }
    const QJsonObject root
    {
        {"format", kFormat},
        {"version", kVersion},
        {"name", profile.name},
        {"matchers", matchers},
        {"headerLines", headers},
        {"dataStart", profile.dataStart},
        {"separators", profile.separators},
        {"decimalComma", profile.decimalComma},
        {"calculators", calculators},
        {"mappings", mappings},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool DataProfileFile::fromJson(const QByteArray &json, DataProfile *profile, QString *error)
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
    const QJsonObject root = document.object();
    if (!document.isObject() || root.value("format").toString() != QLatin1String(kFormat))
        {
            return fail(tr("This is not a data profile."));
        }
    if (root.value("version").toInt() > kVersion)
        {
            return fail(tr("The data profile was saved by a newer version of the program."));
        }
    DataProfile loaded;
    loaded.name = root.value("name").toString();
    for (const QJsonValue &value : root.value("matchers").toArray())
        {
            const QJsonObject object = value.toObject();
            loaded.matchers.append({object.value("line").toInt(), object.value("text").toString(),
                                    !object.value("wholeLine").toBool(), object.value("ignoreCase").toBool()});
        }
    loaded.headerLines.clear();
    for (const QJsonValue &value : root.value("headerLines").toArray())
        {
            loaded.headerLines.append(value.toInt());
        }
    loaded.dataStart = root.value("dataStart").toInt(1);
    loaded.separators = root.value("separators").toString(",");
    loaded.decimalComma = root.value("decimalComma").toBool();
    for (const QJsonValue &value : root.value("calculators").toArray())
        {
            loaded.calculators.append(calculatorFromJson(value.toObject()));
        }
    for (const QJsonValue &value : root.value("mappings").toArray())
        {
            const QJsonObject object = value.toObject();
            DataProfile::Mapping m;
            m.column = object.value("column").toString();
            m.contains = object.value("contains").toBool();
            m.ignoreCase = object.value("ignoreCase").toBool();
            m.role = DataProfile::Mapping::Role(indexOf(kRoles, object.value("role").toString()));
            m.channel = object.value("channel").toString();
            m.unit = object.value("unit").toString();
            loaded.mappings.append(m);
        }
    if (loaded.headerLines.isEmpty() || loaded.separators.isEmpty() || loaded.mappings.isEmpty())
        {
            return fail(tr("The data profile is incomplete."));
        }
    *profile = loaded;
    return true;
}
