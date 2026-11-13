#include "csvimporter.h"

#include "../datatypes.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.14159265358979323846;

QStringList tokens(const QString &line, const QString &separators)
{
    QStringList result;
    QString current;
    for (const QChar c : line)
        {
            if (separators.contains(c))
                {
                    result << current;
                    current.clear();
                }
            else
                {
                    current += c;
                }
        }
    result << current;
    for (QString &token : result)
        {
            token = token.trimmed();
            if (token.size() >= 2 && token.startsWith('"') && token.endsWith('"'))
                {
                    token = token.mid(1, token.size() - 2).trimmed();
                }
        }
    return result;
}

// The start of a day in UTC; QDateTime's time zone constructors differ between Qt versions.
QDateTime utcMidnight(const QDate &date)
{
    return QDateTime::fromSecsSinceEpoch(QDate(1970, 1, 1).daysTo(date) * 86400).toUTC();
}

// What a column has said about absolute time: its first clock time or date.
struct ColumnTime
{
    bool seen{false};
    double first{0.0};         // seconds: since the epoch for dates, since midnight for clock times
    bool hasDate{false};
    bool hasTime{false};
    QDate date;
    double timeOfDay{0.0};     // seconds since midnight
    int row{-1};
};

// "14:05:31.25" -> seconds since midnight
bool clockTime(const QString &text, double *seconds)
{
    static const QRegularExpression pattern("^(\\d{1,2}):(\\d{2}):(\\d{2})(?:[.,](\\d+))?\\s*(AM|PM|am|pm)?$");
    const QRegularExpressionMatch m = pattern.match(text);
    if (!m.hasMatch())
        {
            return false;
        }
    int hours = m.captured(1).toInt();
    const QString half = m.captured(5).toUpper();
    if (half == "PM" && hours < 12)
        {
            hours += 12;
        }
    else if (half == "AM" && hours == 12)
        {
            hours = 0;
        }
    const QString fraction = m.captured(4);
    *seconds = hours * 3600.0 + m.captured(2).toInt() * 60.0 + m.captured(3).toInt()
               + (fraction.isEmpty() ? 0.0 : ("0." + fraction).toDouble());
    return true;
}

// "2025-07-15", "15.07.2025", "7/15/2025" (day first when it cannot be the month)
bool calendarDate(const QString &text, QDate *date)
{
    static const QRegularExpression yearFirst("^(\\d{4})[-/.](\\d{1,2})[-/.](\\d{1,2})$");
    static const QRegularExpression yearLast("^(\\d{1,2})([-/.])(\\d{1,2})[-/.](\\d{4})$");
    QRegularExpressionMatch m = yearFirst.match(text);
    if (m.hasMatch())
        {
            *date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
            return date->isValid();
        }
    m = yearLast.match(text);
    if (!m.hasMatch())
        {
            return false;
        }
    const int a = m.captured(1).toInt();
    const int b = m.captured(3).toInt();
    // with dots it is day.month; with slashes month/day, unless the first cannot be a month
    const bool dayFirst = m.captured(2) == "." || m.captured(2) == "-" || a > 12;
    *date = dayFirst ? QDate(m.captured(4).toInt(), b, a) : QDate(m.captured(4).toInt(), a, b);
    return date->isValid();
}

// A date, a clock time, or both ("2025-07-15T07:47:11Z", "15.07.2025 07:47:11").
bool dateTime(const QString &text, QDate *date, double *timeOfDay, bool *hasDate, bool *hasTime)
{
    QString rest = text;
    if (rest.endsWith('Z'))
        {
            rest.chop(1);
        }
    const int split = std::max(int(rest.indexOf('T')), int(rest.indexOf(' ')));
    *hasDate = false;
    *hasTime = false;
    // a date, then the time: "15.07.2025 07:47:11" (but "11:59:59 PM" is a time alone)
    if (split > 0 && calendarDate(rest.left(split), date))
        {
            *hasDate = true;
            *hasTime = clockTime(rest.mid(split + 1).trimmed(), timeOfDay);
            return *hasTime;
        }
    *hasDate = calendarDate(rest, date);
    *hasTime = !*hasDate && clockTime(rest, timeOfDay);
    return *hasDate || *hasTime;
}

double parseCell(QString text, bool decimalComma, ColumnTime *time, int row)
{
    if (text.isEmpty())
        {
            return kNaN;
        }
    if (decimalComma)
        {
            text.replace(',', '.');
        }
    bool ok = false;
    const double number = text.toDouble(&ok);
    if (ok)
        {
            return number;
        }
    QDate date;
    double timeOfDay = 0.0;
    bool hasDate = false;
    bool hasTime = false;
    if (dateTime(text, &date, &timeOfDay, &hasDate, &hasTime))
        {
            const double absolute = hasDate ? utcMidnight(date).toSecsSinceEpoch() + timeOfDay
                                    : timeOfDay;
            if (!time->seen)
                {
                    time->seen = true;
                    time->first = absolute;
                    time->hasDate = hasDate;
                    time->hasTime = hasTime;
                    time->date = date;
                    time->timeOfDay = timeOfDay;
                    time->row = row;
                }
            double seconds = absolute - time->first;
            // a clock time alone runs over midnight
            if (!hasDate && seconds < -12 * 3600.0)
                {
                    seconds += 24 * 3600.0;
                }
            return seconds;
        }
    const QChar last = text.at(text.size() - 1).toUpper();
    const int hemisphere = last == 'N' || last == 'E' ? 1 : last == 'S' || last == 'W' ? -1 : 0;
    if (hemisphere != 0)
        {
            if (text.size() == 1)
                {
                    return hemisphere;
                }
            const double value = text.left(text.size() - 1).trimmed().toDouble(&ok);
            if (ok)
                {
                    return value * hemisphere;
                }
        }
    // minutes:seconds(.fraction), or 1' 23" 456
    static const QRegularExpression span("^(\\d+):(\\d+)(?:[.,](\\d+))?$");
    static const QRegularExpression marks("^(\\d+)' *(\\d+)\"(?: *(\\d+))?$");
    QRegularExpressionMatch m = span.match(text);
    if (!m.hasMatch())
        {
            m = marks.match(text);
        }
    if (m.hasMatch())
        {
            // thousandths: "5" is 500, "45" 450
            const QString millis = (m.captured(3) + "000").left(3);
            return m.captured(1).toInt() * 60.0 + m.captured(2).toInt() + millis.toInt() / 1000.0;
        }
    return kNaN;
}

using Column = QVector<double>;

double mathOperation(const QString &operation, double a, double b)
{
    if (operation == "add")
        {
            return a + b;
        }
    if (operation == "subtract")
        {
            return a - b;
        }
    if (operation == "multiply")
        {
            return a * b;
        }
    if (operation == "divide")
        {
            return a / b;
        }
    if (operation == "sqrt")
        {
            return std::sqrt(a);
        }
    if (operation == "power")
        {
            return std::pow(a, b);
        }
    if (operation == "modulus")
        {
            return std::fmod(a, b);
        }
    if (operation == "log")
        {
            return std::log(a) / std::log(b);
        }
    if (operation == "hypot")
        {
            return std::hypot(a, b);
        }
    if (operation == "sin")
        {
            return std::sin(a);
        }
    if (operation == "cos")
        {
            return std::cos(a);
        }
    if (operation == "tan")
        {
            return std::tan(a);
        }
    if (operation == "asin")
        {
            return std::asin(a);
        }
    if (operation == "acos")
        {
            return std::acos(a);
        }
    if (operation == "atan")
        {
            return std::atan(a);
        }
    if (operation == "inverse")
        {
            return a == 0.0 ? 0.0 : 1.0 / a;
        }
    return kNaN;
}

double angleOperation(const QString &operation, double v)
{
    const double degree = kPi / 180.0;
    if (operation == "gradeToDegrees")
        {
            return std::atan(v / 100.0) / degree;
        }
    if (operation == "gradeToRadians")
        {
            return std::atan(v / 100.0);
        }
    if (operation == "degreesToGrade")
        {
            return std::tan(v * degree) * 100.0;
        }
    if (operation == "radiansToGrade")
        {
            return std::tan(v) * 100.0;
        }
    if (operation == "degreesToRadians")
        {
            return v * degree;
        }
    if (operation == "radiansToDegrees")
        {
            return v / degree;
        }
    return v;
}

Column fill(const Column &input, const Column &time, double deviations)
{
    Column values = input;
    if (deviations > 0.0)
        {
            // values far from the mean are glitches
            double sum = 0.0;
            int count = 0;
            for (const double v : values)
                {
                    if (std::isfinite(v))
                        {
                            sum += v;
                            ++count;
                        }
                }
            const double mean = count > 0 ? sum / count : 0.0;
            double squares = 0.0;
            for (const double v : values)
                {
                    if (std::isfinite(v))
                        {
                            squares += (v - mean) * (v - mean);
                        }
                }
            const double limit = std::sqrt(squares / std::max(count, 1)) * deviations;
            for (double &v : values)
                {
                    if (std::isfinite(v) && std::fabs(v - mean) > limit)
                        {
                            v = kNaN;
                        }
                }
        }
    Column result(values.size(), kNaN);
    int previous = -1;
    for (int i = 0; i < values.size(); ++i)
        {
            if (!std::isfinite(values[i]))
                {
                    continue;
                }
            result[i] = values[i];
            if (previous < 0)
                {
                    std::fill(result.begin(), result.begin() + i, values[i]);
                }
            else if (i > previous + 1)
                {
                    const double span = time[i] - time[previous];
                    for (int j = previous + 1; j < i; ++j)
                        {
                            const double f = span != 0.0 && std::isfinite(span) ? (time[j] - time[previous]) / span : 0.0;
                            result[j] = values[previous] + (values[i] - values[previous]) * f;
                        }
                }
            previous = i;
        }
    if (previous >= 0)
        {
            std::fill(result.begin() + previous + 1, result.end(), values[previous]);
        }
    return result;
}

Column average(const Column &input, const Column &time, const DataProfile::Calculator &c)
{
    Column result(input.size(), kNaN);
    const double degree = kPi / 180.0;
    for (int i = 0; i < input.size(); ++i)
        {
            double sum = 0.0;
            double sx = 0.0;
            double sy = 0.0;
            int count = 0;
            for (int j = i; j >= 0; --j)
                {
                    if (!c.before && j < i)
                        {
                            break;
                        }
                    if (time[i] - time[j] > c.window)
                        {
                            break;
                        }
                    if (std::isfinite(input[j]))
                        {
                            sum += input[j];
                            sx += std::cos(input[j] * degree);
                            sy += std::sin(input[j] * degree);
                            ++count;
                        }
                }
            for (int j = i + 1; c.after && j < input.size() && time[j] - time[i] <= c.window; ++j)
                {
                    if (std::isfinite(input[j]))
                        {
                            sum += input[j];
                            sx += std::cos(input[j] * degree);
                            sy += std::sin(input[j] * degree);
                            ++count;
                        }
                }
            if (count == 0)
                {
                    continue;
                }
            if (c.angle)
                {
                    const double angle = std::atan2(sy, sx) / degree;
                    result[i] = angle < 0.0 ? angle + 360.0 : angle;
                }
            else
                {
                    result[i] = sum / count;
                }
        }
    return result;
}

// The column a mapping names: by its exact name or a part of it.
int findColumn(const QStringList &names, const DataProfile::Mapping &m)
{
    const Qt::CaseSensitivity cs = m.ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    for (int i = 0; i < names.size(); ++i)
        {
            if (m.contains ? names.at(i).contains(m.column, cs) : names.at(i).compare(m.column, cs) == 0)
                {
                    return i;
                }
        }
    return -1;
}
}

QStringList CsvImporter::lines(const QByteArray &data)
{
    QByteArray bytes = data.startsWith("\xEF\xBB\xBF") ? data.mid(3) : data;
    QString text = QString::fromUtf8(bytes);
    // not UTF-8: loggers on Windows write their own code page
    if (text.contains(QChar::ReplacementCharacter))
        {
            text = QString::fromLatin1(bytes);
        }
    // \r\r\n, written by some loggers, is one line end too
    text.replace("\r\r\n", "\n");
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text.split('\n');
}

int CsvImporter::findProfile(const QStringList &lines, const QVector<DataProfile> &profiles)
{
    for (int i = 0; i < profiles.size(); ++i)
        {
            if (profiles.at(i).matches(lines))
                {
                    return i;
                }
        }
    return -1;
}

TelemetrySessionPtr CsvImporter::load(const QString &path, const QVector<DataProfile> &profiles, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                {
                    *error = tr("Cannot open %1: %2").arg(path, file.errorString());
                }
            return nullptr;
        }
    const QByteArray data = file.readAll();
    const int found = findProfile(lines(data), profiles);
    if (found < 0)
        {
            if (error)
                {
                    *error = profiles.isEmpty()
                             ? tr("%1: there are no data profiles to read CSV files with. Import some first (Library, Import).")
                             .arg(QFileInfo(path).fileName())
                             : tr("%1: none of the %n data profile(s) recognises this file.", nullptr, int(profiles.size()))
                             .arg(QFileInfo(path).fileName());
                }
            return nullptr;
        }
    return parse(data, profiles.at(found), error);
}

TelemetrySessionPtr CsvImporter::parse(const QByteArray &data, const DataProfile &profile, QString *error)
{
    auto fail = [error](const QString &message)
    {
        if (error)
            {
                *error = message;
            }
        return nullptr;
    };
    const QStringList text = lines(data);

    // column names: one header line, or several joined
    QStringList names;
    for (int h = 0; h < profile.headerLines.size(); ++h)
        {
            const int index = profile.headerLines.at(h);
            if (index < 0 || index >= text.size())
                {
                    return fail(tr("The file has no line %1 for the column names.").arg(index + 1));
                }
            const QStringList row = tokens(text.at(index).trimmed(), profile.separators);
            if (h == 0)
                {
                    if (row.size() < 2)
                        {
                            return fail(tr("Line %1 does not hold column names.").arg(index + 1));
                        }
                    names = row;
                }
            else if (row.size() == names.size())
                {
                    for (int i = 0; i < names.size(); ++i)
                        {
                            names[i] += " - " + row.at(i);
                        }
                }
            else
                {
                    return fail(tr("The column name lines differ in length."));
                }
        }

    // the cells
    QVector<Column> columns(names.size());
    QVector<ColumnTime> times(names.size());
    int rows = 0;
    for (int line = std::max(0, profile.dataStart); line < text.size(); ++line)
        {
            const QString trimmed = text.at(line).trimmed();
            if (trimmed.isEmpty())
                {
                    continue;
                }
            const QStringList cells = tokens(trimmed, profile.separators);
            for (int i = 0; i < names.size(); ++i)
                {
                    columns[i].append(i < cells.size() ? parseCell(cells.at(i), profile.decimalComma, &times[i], rows) : kNaN);
                }
            ++rows;
        }
    if (rows == 0)
        {
            return fail(tr("The file has no data."));
        }

    // calculated columns, in as many rounds as their inputs need
    QHash<QString, Column> calculated;
    auto column = [&](const QString &name) -> const Column *
    {
        const int index = int(names.indexOf(name));
        if (index >= 0)
            {
                return &columns.at(index);
            }
        const auto found = calculated.constFind(name);
        return found == calculated.constEnd() ? nullptr : &found.value();
    };
    // the time base, for calculators that work along time
    Column time(rows, kNaN);
    auto findTime = [&]()
    {
        for (const DataProfile::Mapping &m : profile.mappings)
            {
                if (m.role != DataProfile::Mapping::RunningTime)
                    {
                        continue;
                    }
                QStringList available = names;
                available << calculated.keys();
                const int index = findColumn(available, m);
                if (index >= 0)
                    {
                        time = *column(available.at(index));
                        return true;
                    }
            }
        return false;
    };
    bool haveTime = findTime();
    QVector<bool> done(profile.calculators.size(), false);
    bool progress = true;
    while (progress)
        {
            progress = false;
            for (int k = 0; k < profile.calculators.size(); ++k)
                {
                    const DataProfile::Calculator &c = profile.calculators.at(k);
                    if (done[k] || c.inputs.isEmpty() || c.outputs.isEmpty())
                        {
                            continue;
                        }
                    const Column *input = column(c.inputs.first());
                    const bool second = c.type == DataProfile::Calculator::Math && !c.constant;
                    const Column *other = second ? column(c.inputs.value(1)) : nullptr;
                    const bool alongTime = c.type == DataProfile::Calculator::Fill || c.type == DataProfile::Calculator::Average;
                    if (!input || (second && !other) || (alongTime && !haveTime))
                        {
                            continue;
                        }
                    Column out(rows, kNaN);
                    switch (c.type)
                        {
                        case DataProfile::Calculator::Convert:
                            for (int r = 0; r < rows; ++r)
                                {
                                    out[r] = input->at(r) * c.scale + c.offset;
                                }
                            break;
                        case DataProfile::Calculator::Math:
                            for (int r = 0; r < rows; ++r)
                                {
                                    out[r] = mathOperation(c.operation, input->at(r), second ? other->at(r) : c.parameter);
                                }
                            break;
                        case DataProfile::Calculator::Angle:
                            for (int r = 0; r < rows; ++r)
                                {
                                    out[r] = angleOperation(c.operation, input->at(r));
                                }
                            break;
                        case DataProfile::Calculator::Fill:
                            out = fill(*input, time, c.deviations);
                            break;
                        case DataProfile::Calculator::Average:
                            out = average(*input, time, c);
                            break;
                        case DataProfile::Calculator::Delta:
                            for (int r = 1; r < rows; ++r)
                                {
                                    out[r] = input->at(r) - input->at(r - 1);
                                }
                            out[0] = 0.0;
                            break;
                        case DataProfile::Calculator::Previous:
                        {
                            double last = 0.0;
                            for (int r = 0; r < rows; ++r)
                                {
                                    if (std::isfinite(input->at(r)))
                                        {
                                            last = input->at(r);
                                        }
                                    out[r] = last;
                                }
                            break;
                        }
                        }
                    calculated.insert(c.outputs.first(), out);
                    done[k] = true;
                    progress = true;
                    if (!haveTime)
                        {
                            haveTime = findTime();
                        }
                }
        }
    if (!haveTime)
        {
            return fail(tr("The file has no time column the profile knows."));
        }

    // the time base: seconds from the first row that has one
    double t0 = kNaN;
    for (const double t : time)
        {
            if (std::isfinite(t))
                {
                    t0 = t;
                    break;
                }
        }
    if (!std::isfinite(t0))
        {
            return fail(tr("The time column is empty."));
        }

    auto session = std::make_shared<TelemetrySession>();
    session->setSourceInfo(tr("CSV, data profile \"%1\"").arg(profile.name));
    QStringList available = names;
    available << calculated.keys();
    QStringList filled;
    const Column *lapValues = nullptr;
    const ColumnTime *timeOfDay = nullptr;
    const ColumnTime *dateOnly = nullptr;
    for (const DataProfile::Mapping &m : profile.mappings)
        {
            const int index = findColumn(available, m);
            if (index < 0)
                {
                    continue;
                }
            const Column *values = column(available.at(index));
            const ColumnTime *columnTime = index < names.size() ? &times.at(index) : nullptr;
            switch (m.role)
                {
                case DataProfile::Mapping::RunningTime:
                    break;
                case DataProfile::Mapping::TimeOfDay:
                    timeOfDay = timeOfDay ? timeOfDay : columnTime;
                    break;
                case DataProfile::Mapping::Date:
                    dateOnly = dateOnly ? dateOnly : columnTime;
                    break;
                case DataProfile::Mapping::Lap:
                    if (!lapValues)
                        {
                            lapValues = values;
                        }
                    break;
                case DataProfile::Mapping::Value:
                {
                    // the first column found for a measurement wins (a file may have it in mph and km/h)
                    if (m.channel.isEmpty() || filled.contains(m.channel))
                        {
                            break;
                        }
                    double scale = 1.0;
                    double offset = 0.0;
                    QString stored;
                    DataTypes::toStored(m.unit, &scale, &offset, &stored);
                    if (stored.isEmpty())
                        {
                            stored = DataTypes::storedUnitOf(m.channel);
                        }
                    const bool steps = m.channel == QLatin1String("lap") || m.channel == QLatin1String("gear");
                    Channel &channel = session->channelForWriting(m.channel, stored, steps ? Channel::Step : Channel::Linear);
                    for (int r = 0; r < rows; ++r)
                        {
                            if (std::isfinite(time[r]) && std::isfinite(values->at(r)))
                                {
                                    channel.append(time[r] - t0, values->at(r) * scale + offset);
                                }
                        }
                    filled << m.channel;
                    break;
                }
                }
        }
    session->removeEmptyChannels();
    if (session->channelIds().isEmpty())
        {
            return fail(tr("None of the columns this data profile names are in the file."));
        }

    // laps: a new one wherever the lap column changes to something other than 0
    if (lapValues)
        {
            QVector<double> starts;
            double previous = kNaN;
            for (int r = 0; r < rows; ++r)
                {
                    const double v = lapValues->at(r);
                    if (!std::isfinite(v) || !std::isfinite(time[r]))
                        {
                            continue;
                        }
                    if (std::isfinite(previous) && v != previous && v != 0.0)
                        {
                            starts << time[r] - t0;
                        }
                    previous = v;
                }
            for (int i = 0; i + 1 < starts.size(); ++i)
                {
                    session->addLap({starts.at(i), starts.at(i + 1)});
                }
        }

    // when it started: a clock time with its date, the date from a column of its own if need be
    if (timeOfDay && timeOfDay->seen && timeOfDay->hasTime)
        {
            QDate date = timeOfDay->hasDate ? timeOfDay->date : QDate();
            if (!date.isValid() && dateOnly && dateOnly->seen && dateOnly->hasDate)
                {
                    date = dateOnly->date;
                }
            if (date.isValid())
                {
                    const double atRow = std::isfinite(time[timeOfDay->row]) ? time[timeOfDay->row] - t0 : 0.0;
                    const QDateTime moment = utcMidnight(date)
                                             .addMSecs(qint64(std::llround((timeOfDay->timeOfDay - atRow) * 1000.0)));
                    session->setStartUtc(moment);
                }
        }
    return session;
}
