#include "telemetry/channelids.h"
#include "telemetry/csv/csvimporter.h"
#include "telemetry/csv/dataprofilelibrary.h"
#include "telemetry/csv/xmldataprofileimporter.h"
#include "telemetry/datatypes.h"
#include "telemetry/telemetryimporter.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

// A moment in UTC, built the same way on every Qt version.
static QDateTime utc(const QDate &date, const QTime &time)
{
    return QDateTime::fromMSecsSinceEpoch(QDate(1970, 1, 1).daysTo(date) * 86400000 + time.msecsSinceStartOfDay()).toUTC();
}

class TestCsv : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // A profile reading "Time" as seconds and everything else as `mappings` say.
    static DataProfile profile(const QVector<DataProfile::Mapping> &mappings = {})
    {
        DataProfile p;
        p.name = "Test";
        p.matchers = {{0, "TestLogger", true, false}};
        p.headerLines = {1};
        p.dataStart = 2;
        p.separators = ",";
        DataProfile::Mapping time;
        time.column = "Time";
        time.role = DataProfile::Mapping::RunningTime;
        p.mappings = {time};
        p.mappings += mappings;
        return p;
    }

    static DataProfile::Mapping value(const QString &column, const QString &channel, const QString &unit = QString())
    {
        DataProfile::Mapping m;
        m.column = column;
        m.channel = channel;
        m.unit = unit;
        return m;
    }

    static TelemetrySessionPtr read(const QByteArray &csv, const DataProfile &p)
    {
        QString error;
        TelemetrySessionPtr session = CsvImporter::parse(csv, p, &error);
        if (!session)
            {
                qWarning("%s", qPrintable(error));
            }
        return session;
    }

    QString write(const QString &name, const QByteArray &content)
    {
        const QString path = m_dir.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            {
                file.write(content);
            }
        return path;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        DataProfileLibrary::setUserFolder(m_dir.filePath("profiles"));
    }

    void dataTypes()
    {
        QString unit;
        QCOMPARE(DataTypes::channelFor("Speed (KPH)", &unit), QString("speed"));
        QCOMPARE(unit, QString("km/h"));
        QCOMPARE(DataTypes::channelFor("Vertical Speed (FPM)", &unit), QString("verticalSpeed"));
        QCOMPARE(unit, QString("ft/min"));
        double scale = 0.0;
        double offset = 0.0;
        QString stored;
        QVERIFY(DataTypes::toStored("mph", &scale, &offset, &stored));
        QCOMPARE(stored, QString("m/s"));
        QCOMPARE(100.0 * scale + offset, 44.704);
        QVERIFY(DataTypes::toStored(QString::fromUtf8("°F"), &scale, &offset, &stored));
        QCOMPARE(212.0 * scale + offset, 100.0);
        QVERIFY(!DataTypes::toStored("psi", &scale, &offset, &stored));
        QCOMPARE(stored, QString("psi"));
        QCOMPARE(DataTypes::storedUnitOf(ChannelId::PositionLat), QString("deg"));
        QCOMPARE(DataTypes::storedUnitOf("engineRpm"), QString());
    }

    void matchers()
    {
        DataProfile p = profile();
        p.matchers = {{0, "testlogger", true, true}, {1, "Time,Speed", false, false}};
        QVERIFY(p.matches({"Made by TestLogger 2.0", "  Time,Speed  "}));
        QVERIFY(!p.matches({"Made by TestLogger 2.0", "Time,Speed,More"}));     // the whole line
        QVERIFY(!p.matches({"Made by TestLogger 2.0"}));                         // too short
        QCOMPARE(p.linesToMatch(), 2);
        p.matchers.clear();
        QVERIFY(!p.matches({"anything"}));
        const DataProfile other = profile();
        QCOMPARE(CsvImporter::findProfile({"TestLogger"}, {p, other}), 1);
        QCOMPARE(CsvImporter::findProfile({"Something else"}, {p, other}), -1);
    }

    void valuesAreConvertedAndStored()
    {
        const QByteArray csv = "TestLogger export\r\n"
                               "Time,Speed (mph),Alt ft,Lat,Lon,Temp F,RPM\r\n"
                               "0,0,1000,49.5,9.5,32,800\r\n"
                               "1,22.369362920544,1010,49.5001,9.5001,50,\r\n"
                               "\r\n"
                               "2,44.738725841088,1020,49.5002,9.5002,212,3000\r\n";
        const TelemetrySessionPtr s = read(csv, profile({value("Speed (mph)", "speed", "mph"), value("Alt ft", "elevation", "ft"),
                                                        value("Lat", "positionLat"), value("Lon", "positionLon"),
                                                        value("Temp F", "temperature", QString::fromUtf8("°F")),
                                                        value("RPM", "engineRpm")}));
        QVERIFY(s);
        const Channel *speed = s->channel("speed");
        QVERIFY(speed);
        QCOMPARE(speed->unit(), QString("m/s"));
        QCOMPARE(speed->count(), 3);
        QVERIFY(std::fabs(speed->valueAt(1.0) - 10.0) < 1e-9);
        QVERIFY(std::fabs(s->channel("elevation")->valueAt(2.0) - 1020 * 0.3048) < 1e-9);
        QCOMPARE(s->channel("positionLat")->unit(), QString("deg"));
        QVERIFY(std::fabs(s->channel("temperature")->valueAt(2.0) - 100.0) < 1e-9);
        // an empty cell is no sample
        QCOMPARE(s->channel("engineRpm")->count(), 2);
        QCOMPARE(s->channel("engineRpm")->unit(), QString());
        QVERIFY(s->sourceInfo().contains("Test"));
    }

    void cellsOfManyKinds()
    {
        DataProfile p = profile({value("Lap time", "lapTime"), value("Lat", "positionLat"), value("Hem", "hemisphere"),
                                 value("Value", "value")});
        p.separators = ";\t";
        p.decimalComma = true;
        const QByteArray csv = "TestLogger\n"
                               "\"Time\";\"Lap time\";\"Lat\";\"Hem\";\"Value\"\n"
                               "12:00:00,5;1:02,5;49,5N;S;1,5\n"
                               "12:00:01,5;1'03\" 25;12,25S;E;\"2,5\"\n"
                               "12:00:02;nonsense;;N;3\n";
        const TelemetrySessionPtr s = read(csv, p);
        QVERIFY(s);
        // clock times count from the first one
        QCOMPARE(s->channel("value")->startTime(), 0.0);
        QCOMPARE(s->channel("value")->endTime(), 1.5);
        QCOMPARE(s->channel("value")->valueAt(1.0), 2.5);
        QCOMPARE(s->channel("lapTime")->valueAt(0.0), 62.5);
        QCOMPARE(s->channel("lapTime")->valueAt(1.0), 63.25);
        QCOMPARE(s->channel("lapTime")->count(), 2);
        QCOMPARE(s->channel("positionLat")->valueAt(0.0), 49.5);
        QCOMPARE(s->channel("positionLat")->valueAt(1.0), -12.25);
        QCOMPARE(s->channel("hemisphere")->valueAt(0.0), -1.0);
        QCOMPARE(s->channel("hemisphere")->valueAt(1.0), 1.0);
    }

    void calculatorsWorkColumnsIntoOthers()
    {
        DataProfile p = profile({value("Speed", "speed", "mph"), value("Neg G", "gForceLateral"), value("Total", "total"),
                                 value("Grade deg", "gradeAngle"), value("Filled", "filled"), value("Smooth", "smooth"),
                                 value("Step", "step"), value("Held", "held")});
        auto calculator = [](DataProfile::Calculator::Type type, const QStringList &in, const QString &out)
        {
            DataProfile::Calculator c;
            c.type = type;
            c.inputs = in;
            c.outputs = {out};
            return c;
        };
        // listed out of order: the speed needs the metres per second first
        DataProfile::Calculator mph = calculator(DataProfile::Calculator::Convert, {"m/s"}, "Speed");
        mph.scale = 2.2369362920544;
        DataProfile::Calculator ms = calculator(DataProfile::Calculator::Math, {"cm/s"}, "m/s");
        ms.operation = "divide";
        ms.parameter = 100;
        DataProfile::Calculator negate = calculator(DataProfile::Calculator::Math, {"G"}, "Neg G");
        negate.operation = "multiply";
        negate.parameter = -1;
        DataProfile::Calculator sum = calculator(DataProfile::Calculator::Math, {"A", "B"}, "Total");
        sum.operation = "add";
        sum.constant = false;
        DataProfile::Calculator angle = calculator(DataProfile::Calculator::Angle, {"Grade"}, "Grade deg");
        angle.operation = "gradeToDegrees";
        DataProfile::Calculator gaps = calculator(DataProfile::Calculator::Fill, {"Gappy"}, "Filled");
        DataProfile::Calculator smooth = calculator(DataProfile::Calculator::Average, {"A"}, "Smooth");
        smooth.window = 1.0;
        p.calculators = {mph, ms, negate, sum, angle, gaps, smooth,
                         calculator(DataProfile::Calculator::Delta, {"B"}, "Step"),
                         calculator(DataProfile::Calculator::Previous, {"Gappy"}, "Held")};
        const QByteArray csv = "TestLogger\n"
                               "Time,cm/s,G,A,B,Grade,Gappy\n"
                               "0,1000,0.5,1,10,100,0\n"
                               "1,2000,-0.25,2,20,0,\n"
                               "2,3000,0,3,40,-100,\n"
                               "3,4000,0,6,70,0,30\n";
        const TelemetrySessionPtr s = read(csv, p);
        QVERIFY(s);
        // 2000 cm/s -> 20 m/s -> 44.7 mph in the column -> 20 m/s stored
        QVERIFY(std::fabs(s->channel("speed")->valueAt(1.0) - 20.0) < 1e-9);
        QVERIFY(std::fabs(s->channel("speed")->valueAt(2.0) - 30.0) < 1e-9);
        QCOMPARE(s->channel("gForceLateral")->valueAt(1.0), 0.25);
        QCOMPARE(s->channel("total")->valueAt(2.0), 43.0);
        QVERIFY(std::fabs(s->channel("gradeAngle")->valueAt(0.0) - 45.0) < 1e-9);
        QVERIFY(std::fabs(s->channel("gradeAngle")->valueAt(2.0) + 45.0) < 1e-9);
        QCOMPARE(s->channel("filled")->count(), 4);
        QCOMPARE(s->channel("filled")->valueAt(2.0), 20.0);
        QCOMPARE(s->channel("smooth")->valueAt(1.0), 2.0);
        QCOMPARE(s->channel("smooth")->valueAt(3.0), 4.5);
        QCOMPARE(s->channel("step")->valueAt(3.0), 30.0);
        QCOMPARE(s->channel("held")->valueAt(2.0), 0.0);
        QCOMPARE(s->channel("held")->valueAt(3.0), 30.0);
    }

    void firstColumnFoundWinsAndNamesMayMatchInPart()
    {
        DataProfile::Mapping kph = value("speed (KPH)", "speed", "km/h");
        kph.contains = true;
        kph.ignoreCase = true;
        DataProfile p = profile({kph, value("Speed MPH", "speed", "mph")});
        const QByteArray csv = "TestLogger\nTime,Vehicle Speed (kph),Speed MPH\n0,36,99\n1,72,99\n";
        const TelemetrySessionPtr s = read(csv, p);
        QVERIFY(s);
        QCOMPARE(s->channel("speed")->valueAt(1.0), 20.0);
    }

    void headerLinesAreJoined()
    {
        DataProfile p = profile({value("Speed - [m/s]", "speed", "m/s")});
        p.headerLines = {1, 2};
        p.dataStart = 3;
        const QByteArray csv = "TestLogger\nTime,Speed\n[s],[m/s]\n0,5\n1,6\n";
        // the time column is "Time - [s]" now: as "Time" it is not found
        QVERIFY(!CsvImporter::parse(csv, p));
        p.mappings[0].column = "Time - [s]";
        const TelemetrySessionPtr s = read(csv, p);
        QVERIFY(s);
        QCOMPARE(s->channel("speed")->valueAt(1.0), 6.0);
    }

    void lapsStartWhereTheLapColumnChanges()
    {
        DataProfile::Mapping lap;
        lap.column = "Lap";
        lap.role = DataProfile::Mapping::Lap;
        const DataProfile p = profile({lap, value("V", "speed", "m/s")});
        QByteArray csv = "TestLogger\nTime,Lap,V\n";
        // a lap number: 0 0 1 1 1 2 2 3
        const int laps[] = {0, 0, 1, 1, 1, 2, 2, 3};
        for (int i = 0; i < 8; ++i)
            {
                csv += QByteArray::number(i) + "," + QByteArray::number(laps[i]) + ",1\n";
            }
        TelemetrySessionPtr s = read(csv, p);
        QVERIFY(s);
        QCOMPARE(s->laps().size(), 2);    // the complete ones
        QCOMPARE(s->laps()[0].startTime, 2.0);
        QCOMPARE(s->laps()[0].endTime, 5.0);
        QCOMPARE(s->laps()[1].endTime, 7.0);
        // or a mark where a lap begins: 0 1 0 0 1 0 0 1
        csv = "TestLogger\nTime,Lap,V\n";
        const int marks[] = {0, 1, 0, 0, 1, 0, 0, 1};
        for (int i = 0; i < 8; ++i)
            {
                csv += QByteArray::number(i) + "," + QByteArray::number(marks[i]) + ",1\n";
            }
        s = read(csv, p);
        QCOMPARE(s->laps().size(), 2);
        QCOMPARE(s->laps()[0].startTime, 1.0);
        QCOMPARE(s->laps()[1].startTime, 4.0);
    }

    void startTimeFromClockAndDate()
    {
        DataProfile::Mapping clock;
        clock.column = "Time";
        clock.role = DataProfile::Mapping::TimeOfDay;
        DataProfile::Mapping date;
        date.column = "Date";
        date.role = DataProfile::Mapping::Date;
        const DataProfile p = profile({clock, date, value("V", "speed", "m/s")});
        // the date in a column of its own, written day first
        TelemetrySessionPtr s = read("TestLogger\nDate,Time,V\n15.07.2025,07:47:11.50,1\n15.07.2025,07:47:12.50,2\n", p);
        QVERIFY(s);
        QCOMPARE(s->startUtc(), utc(QDate(2025, 7, 15), QTime(7, 47, 11, 500)));
        // or both in one cell, and month first when the day cannot be a month
        s = read("TestLogger\nDate,Time,V\nx,2025-07-15T07:47:11Z,1\nx,2025-07-15T07:47:13Z,2\n", p);
        QCOMPARE(s->startUtc(), utc(QDate(2025, 7, 15), QTime(7, 47, 11)));
        QCOMPARE(s->channel("speed")->endTime(), 2.0);
        s = read("TestLogger\nDate,Time,V\n7/15/2025,11:59:59 PM,1\n7/15/2025,12:00:01 AM,2\n", p);
        QCOMPARE(s->startUtc(), utc(QDate(2025, 7, 15), QTime(23, 59, 59)));
        QCOMPARE(s->channel("speed")->endTime(), 2.0);    // over midnight
        // no date: no start
        s = read("TestLogger\nDate,Time,V\n,07:47:11,1\n,07:47:12,2\n", p);
        QVERIFY(!s->startUtc().isValid());
    }

    void whatCannotBeRead()
    {
        QString error;
        QVERIFY(!CsvImporter::parse("TestLogger\n", profile({value("V", "v")}), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!CsvImporter::parse("TestLogger\nTime,V\n", profile({value("V", "v")}), &error));
        QVERIFY(!CsvImporter::parse("TestLogger\nClock,V\n1,2\n", profile({value("V", "v")}), &error));    // no time column
        QVERIFY(!CsvImporter::parse("TestLogger\nTime,W\n1,2\n", profile({value("V", "v")}), &error));     // nothing named
        QVERIFY(error.contains("columns"));
    }

    void lineEndsAndEncodings()
    {
        const QStringList lines = CsvImporter::lines("\xEF\xBB\xBF" "a\r\r\nb\rc\nd");
        QCOMPARE(lines, QStringList({"a", "b", "c", "d"}));
        // Windows code page: not UTF-8
        QCOMPARE(CsvImporter::lines("Temp \xB0" "C").first(), QString::fromUtf8("Temp °C"));
    }

    void jsonRoundTrip()
    {
        DataProfile p = profile({value("Speed", "speed", "mph")});
        p.name = "Round trip";
        p.headerLines = {3, 4};
        p.dataStart = 6;
        p.separators = ",;\t";
        p.decimalComma = true;
        p.matchers.append({2, "Exact", false, true});
        DataProfile::Calculator c;
        c.type = DataProfile::Calculator::Average;
        c.inputs = {"Heading"};
        c.outputs = {"Smooth heading"};
        c.window = 0.25;
        c.angle = true;
        c.after = false;
        DataProfile::Calculator m;
        m.type = DataProfile::Calculator::Math;
        m.inputs = {"A", "B"};
        m.outputs = {"C"};
        m.operation = "hypot";
        m.constant = false;
        DataProfile::Calculator k;
        k.type = DataProfile::Calculator::Convert;
        k.inputs = {"F"};
        k.outputs = {"C"};
        k.scale = 5.0 / 9.0;
        k.offset = -160.0 / 9.0;
        p.calculators = {c, m, k};
        DataProfile::Mapping lap;
        lap.column = "Lap";
        lap.contains = true;
        lap.ignoreCase = true;
        lap.role = DataProfile::Mapping::Lap;
        p.mappings.append(lap);

        DataProfile loaded;
        QString error;
        QVERIFY2(DataProfileFile::fromJson(DataProfileFile::toJson(p), &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, p.name);
        QCOMPARE(loaded.headerLines, p.headerLines);
        QCOMPARE(loaded.dataStart, 6);
        QCOMPARE(loaded.separators, p.separators);
        QVERIFY(loaded.decimalComma);
        QVERIFY(loaded.matchers == p.matchers);
        QVERIFY(loaded.calculators == p.calculators);
        QVERIFY(loaded.mappings == p.mappings);
        QVERIFY(!DataProfileFile::fromJson("{}", &loaded));
        QVERIFY(!DataProfileFile::fromJson("not json", &loaded));
    }

    void xmlProfileIsConverted()
    {
        const QByteArray xml =
            "<DataProfile version=\"2\" readOnly=\"True\" isAimData=\"False\" isDriftBoxData=\"False\">\n"
            "  <HeaderLines><HeaderLineIndex>2</HeaderLineIndex><HeaderLineIndex>3</HeaderLineIndex></HeaderLines>\n"
            "  <DataStartLineIndex>4</DataStartLineIndex>\n"
            "  <DataValueSeparators>,;\t</DataValueSeparators>\n"
            "  <UseEuroDecimalFormat>True</UseEuroDecimalFormat>\n"
            "  <FileLineMatchers>\n"
            "    <FileLineMatcher dataLineIndex=\"0\" matchText=\"Logger X\" matchMode=\"Contains\" ignoreCase=\"True\" />\n"
            "    <FileLineMatcher dataLineIndex=\"1\" matchText=\"Format,1\" matchMode=\"ExactMatch\" ignoreCase=\"False\" />\n"
            "  </FileLineMatchers>\n"
            "  <Calculators>\n"
            "    <SpeedConverter inputUnit=\"m/s\" outputUnit=\"KPH\"><InputColumns><InputColumn>Speed</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>Speed KPH</OutputColumn></OutputColumns></SpeedConverter>\n"
            "    <TemperatureConverter inputUnit=\"Celsius\" outputUnit=\"Fahrenheit\"><InputColumns><InputColumn>T</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>TF</OutputColumn></OutputColumns></TemperatureConverter>\n"
            "    <PressureConverter inputUnit=\"kPa\" outputUnit=\"psi\" makeRelativeToAtmosphere=\"True\"><InputColumns><InputColumn>P</InputColumn>"
            "</InputColumns><OutputColumns><OutputColumn>Boost</OutputColumn></OutputColumns></PressureConverter>\n"
            "    <MathCalculator mathOperation=\"Divide\" parameter=\"0\" useConstantParameter=\"False\"><InputColumns><InputColumn>A</InputColumn>"
            "<InputColumn>B</InputColumn></InputColumns><OutputColumns><OutputColumn>A/B</OutputColumn></OutputColumns></MathCalculator>\n"
            "    <AngleConverter inputUnit=\"Grade Percentage\" outputUnit=\"Degrees\"><InputColumns><InputColumn>G</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>GD</OutputColumn></OutputColumns></AngleConverter>\n"
            "    <LinearInterpolator filterValues=\"True\" standardDevCeiling=\"5\"><InputColumns><InputColumn>X</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>X2</OutputColumn></OutputColumns></LinearInterpolator>\n"
            "    <AveragingCalculator parameter=\"30\" mode=\"Simple\" direction=\"Backwards\"><InputColumns><InputColumn>W</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>W30</OutputColumn></OutputColumns></AveragingCalculator>\n"
            "    <PpmToRpmConverter cylinderCount=\"4\" promptForCylinderCount=\"False\"><InputColumns><InputColumn>PPM</InputColumn></InputColumns>"
            "<OutputColumns><OutputColumn>RPM</OutputColumn></OutputColumns></PpmToRpmConverter>\n"
            "    <DegreesToCartesianConverter inputUnit=\"Degrees\" outputUnit=\"Feet\"><InputColumns><InputColumn>Lon</InputColumn>"
            "<InputColumn>Lat</InputColumn></InputColumns><OutputColumns><OutputColumn>X</OutputColumn><OutputColumn>Y</OutputColumn>"
            "</OutputColumns></DegreesToCartesianConverter>\n"
            "    <LapSummaryCalculator><InputColumns><InputColumn>Speed</InputColumn></InputColumns><OutputColumns><OutputColumn>a</OutputColumn>"
            "</OutputColumns></LapSummaryCalculator>\n"
            "  </Calculators>\n"
            "  <ColumnMappings>\n"
            "    <ColumnMapping inputColumnText=\"Time\" inputColumnMatchMode=\"ExactMatch\" inputColumnIgnoreCase=\"False\" "
            "dataCategory=\"&lt;Required&gt;\" dataType=\"Data Running Time, Seconds\" />\n"
            "    <ColumnMapping inputColumnText=\"Clock\" inputColumnMatchMode=\"ExactMatch\" inputColumnIgnoreCase=\"False\" "
            "dataCategory=\"Date Time\" dataType=\"Time of Day\" />\n"
            "    <ColumnMapping inputColumnText=\"Lap\" inputColumnMatchMode=\"Contains\" inputColumnIgnoreCase=\"True\" "
            "dataCategory=\"&lt;Lap Timing Inputs&gt;\" dataType=\"Lap Indicator\" />\n"
            "    <ColumnMapping inputColumnText=\"Speed KPH\" inputColumnMatchMode=\"ExactMatch\" inputColumnIgnoreCase=\"False\" "
            "dataCategory=\"Velocity\" dataType=\"Speed (KPH)\" />\n"
            "    <ColumnMapping inputColumnText=\"X\" inputColumnMatchMode=\"ExactMatch\" inputColumnIgnoreCase=\"False\" "
            "dataCategory=\"Positional\" dataType=\"Longitude / X Position (Feet)\" />\n"
            "    <ColumnMapping inputColumnText=\"Lat\" inputColumnMatchMode=\"ExactMatch\" inputColumnIgnoreCase=\"False\" "
            "dataCategory=\"Positional\" dataType=\"Latitude / Y Position (Degrees)\" />\n"
            "  </ColumnMappings>\n"
            "</DataProfile>\n";
        const XmlDataProfileImporter::Result result = XmlDataProfileImporter::parse(xml, "Logger X");
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.warnings.join(' ').contains("LapSummaryCalculator"));
        const DataProfile &p = result.profile;
        QCOMPARE(p.name, QString("Logger X"));
        QCOMPARE(p.headerLines, QVector<int>({2, 3}));
        QCOMPARE(p.dataStart, 4);
        QCOMPARE(p.separators, QString(",;\t"));
        QVERIFY(p.decimalComma);
        QCOMPARE(p.matchers.size(), 2);
        QVERIFY(p.matchers[0].contains && p.matchers[0].ignoreCase);
        QVERIFY(!p.matchers[1].contains);

        // projections and derived values left out
        QCOMPARE(p.calculators.size(), 8);
        const DataProfile::Calculator &kph = p.calculators[0];
        QCOMPARE(kph.type, DataProfile::Calculator::Convert);
        QVERIFY(std::fabs(kph.scale - 3.6) < 1e-9);
        const DataProfile::Calculator &f = p.calculators[1];
        QVERIFY(std::fabs(100.0 * f.scale + f.offset - 212.0) < 1e-9);
        const DataProfile::Calculator &boost = p.calculators[2];
        QVERIFY(std::fabs(101.325 * boost.scale + boost.offset) < 1e-9);    // atmospheric pressure reads 0
        QCOMPARE(p.calculators[3].operation, QString("divide"));
        QVERIFY(!p.calculators[3].constant);
        QCOMPARE(p.calculators[4].operation, QString("gradeToDegrees"));
        QCOMPARE(p.calculators[5].type, DataProfile::Calculator::Fill);
        QCOMPARE(p.calculators[5].deviations, 5.0);
        QCOMPARE(p.calculators[6].window, 30.0);
        QVERIFY(p.calculators[6].before && !p.calculators[6].after);
        QCOMPARE(p.calculators[7].scale, 0.5);

        QCOMPARE(p.mappings.size(), 5);
        QCOMPARE(p.mappings[0].role, DataProfile::Mapping::RunningTime);
        QCOMPARE(p.mappings[1].role, DataProfile::Mapping::TimeOfDay);
        QCOMPARE(p.mappings[2].role, DataProfile::Mapping::Lap);
        QVERIFY(p.mappings[2].contains && p.mappings[2].ignoreCase);
        QCOMPARE(p.mappings[3].channel, QString("speed"));
        QCOMPARE(p.mappings[3].unit, QString("km/h"));
        QCOMPARE(p.mappings[4].channel, QString("positionLat"));
        QVERIFY(p.matches(CsvImporter::lines("logger x v2\nFormat,1\n")));
    }

    void xmlProfilesRefused()
    {
        QVERIFY(!XmlDataProfileImporter::parse("<Gauge/>", "x").ok);
        QVERIFY(!XmlDataProfileImporter::parse("<DataProfile isAimData=\"True\"/>", "x").ok);
        // no time column
        QVERIFY(!XmlDataProfileImporter::parse("<DataProfile><FileLineMatchers><FileLineMatcher dataLineIndex=\"0\" matchText=\"a\"/>"
                                               "</FileLineMatchers><ColumnMappings/></DataProfile>", "x").ok);
        // nothing to recognise its files by
        QVERIFY(!XmlDataProfileImporter::parse("<DataProfile><ColumnMappings><ColumnMapping inputColumnText=\"T\" "
                                               "dataType=\"Data Running Time, Seconds\"/></ColumnMappings></DataProfile>", "x").ok);
    }

    void libraryAndTelemetryImporter()
    {
        const QString xml = write("DataProfiles/Logger Y.xml",
                                  "<DataProfile version=\"2\"><HeaderLines><HeaderLineIndex>1</HeaderLineIndex></HeaderLines>"
                                  "<DataStartLineIndex>2</DataStartLineIndex><DataValueSeparators>,</DataValueSeparators>"
                                  "<UseEuroDecimalFormat>False</UseEuroDecimalFormat><FileLineMatchers><FileLineMatcher dataLineIndex=\"0\" "
                                  "matchText=\"Logger Y\" matchMode=\"Contains\" ignoreCase=\"False\"/></FileLineMatchers><Calculators/>"
                                  "<ColumnMappings><ColumnMapping inputColumnText=\"t\" inputColumnMatchMode=\"ExactMatch\" "
                                  "dataType=\"Data Running Time, Seconds\"/><ColumnMapping inputColumnText=\"hr\" "
                                  "inputColumnMatchMode=\"ExactMatch\" dataType=\"Heart Rate\"/></ColumnMappings></DataProfile>");
        const QString bad = write("DataProfiles/broken.xml", "<DataProfile/>");
        QVERIFY(XmlDataProfileImporter::isDataProfile(xml));
        QVERIFY(!XmlDataProfileImporter::isDataProfile(write("other.xml", "<Gauge/>")));
        const DataProfileLibrary::ImportSummary summary = DataProfileLibrary::importXml({xml, bad});
        QCOMPARE(summary.imported, 1);
        QCOMPARE(summary.failed.size(), 1);
        QCOMPARE(DataProfileLibrary::profiles().size(), 1);
        QCOMPARE(DataProfileLibrary::profiles().first().name, QString("Logger Y"));

        // a .csv opens like any data file
        QVERIFY(TelemetryImporter::canLoad("ride.CSV"));
        const QString csv = write("ride.csv", "Logger Y 1.0\nt,hr\n0,100\n1,120\n2,130\n");
        QString error;
        const TelemetrySessionPtr s = TelemetryImporter::load(csv, &error);
        QVERIFY2(s, qPrintable(error));
        QCOMPARE(s->channel(ChannelId::HeartRate)->valueAt(1.0), 120.0);
        QCOMPARE(s->channel(ChannelId::HeartRate)->unit(), QString("bpm"));
        // one nothing recognises
        QVERIFY(!TelemetryImporter::load(write("other.csv", "Someone else\nt,hr\n0,1\n"), &error));
        QVERIFY(error.contains("other.csv"));

        QVERIFY(DataProfileLibrary::remove("Logger Y"));
        QVERIFY(DataProfileLibrary::profiles().isEmpty());
        QVERIFY(!TelemetryImporter::load(csv, &error));
        QVERIFY(error.contains("Import"));
    }

    // Imports every XML data profile in RTV_DATA_PROFILES and reads every CSV
    // file in RTV_CSV_SAMPLES with them, printing what was found.
    void sampleFiles()
    {
        const QString profiles = qEnvironmentVariable("RTV_DATA_PROFILES");
        if (profiles.isEmpty())
            {
                QSKIP("Set RTV_DATA_PROFILES to a folder of XML data profiles (and RTV_CSV_SAMPLES to one of CSV files)");
            }
        DataProfileLibrary::setUserFolder(m_dir.filePath("samples"));
        QStringList files;
        const QDir dir(profiles);
        for (const QString &name : dir.entryList({"*.xml"}, QDir::Files))
            {
                files << dir.filePath(name);
            }
        const DataProfileLibrary::ImportSummary summary = DataProfileLibrary::importXml(files);
        qInfo("%d profiles imported", summary.imported);
        for (const QString &line : summary.failed + summary.notes)
            {
                qInfo("%s", qPrintable(line));
            }
        const QDir samples(qEnvironmentVariable("RTV_CSV_SAMPLES"));
        for (const QString &name : samples.entryList(QDir::Files))
            {
                QString error;
                const TelemetrySessionPtr s = CsvImporter::load(samples.filePath(name), DataProfileLibrary::profiles(), &error);
                qInfo("%s: %s", qPrintable(name), s ? qPrintable(s->sourceInfo() + ": " + s->channelIds().join(", ")) : qPrintable(error));
            }
        DataProfileLibrary::setUserFolder(m_dir.filePath("profiles"));
    }
};

QTEST_MAIN(TestCsv)
#include "test_csv.moc"
