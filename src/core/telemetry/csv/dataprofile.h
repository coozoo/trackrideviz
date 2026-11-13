#pragma once

#include <QCoreApplication>
#include <QStringList>
#include <QVector>

// How to read the CSV files of one kind of logger: how to recognise them,
// where the column names and the data are, which column is what, and how
// columns are worked into others before that (unit conversions, sums,
// smoothing). Kept as JSON in the data profile library.
struct DataProfile
{
    // A text that must be found on a line near the start of the file.
    struct LineMatcher
    {
        int line{0};
        QString text;
        bool contains{true};     // or the whole line, trimmed
        bool ignoreCase{false};

        bool operator==(const LineMatcher &other) const
        {
            return line == other.line && text == other.text && contains == other.contains && ignoreCase == other.ignoreCase;
        }
    };

    // Makes columns out of columns. Inputs and outputs are column names; the
    // outputs can be read by later calculators and by mappings.
    struct Calculator
    {
        enum Type
        {
            Convert,    // output = input * scale + offset
            Math,       // output = input <operation> (parameter or the second input)
            Angle,      // grade (%) <-> degrees <-> radians: `operation` "gradeToDegrees"...
            Fill,       // gaps filled in along time; values far off the mean (in standard deviations) dropped first
            Average,    // mean over `window` seconds before and/or after; `angle`: of directions
            Delta,      // the change since the row before
            Previous    // the last value there was, for empty cells
        };
        Type type{Convert};
        QStringList inputs;
        QStringList outputs;
        double scale{1.0};
        double offset{0.0};
        // Math: add subtract multiply divide sqrt power modulus log hypot sin cos tan asin acos atan inverse
        QString operation;
        bool constant{true};     // Math: the parameter, not the second input
        double parameter{0.0};
        double deviations{0.0};  // Fill: 0 = keep every value
        double window{0.0};      // Average, seconds
        bool before{true};       // Average
        bool after{true};
        bool angle{false};

        bool operator==(const Calculator &other) const;
    };

    struct Mapping
    {
        enum Role
        {
            Value,
            RunningTime,   // seconds; the time base. Clock times count from the first one
            TimeOfDay,     // when the recording started
            Date,
            Lap            // a new lap where the value changes to anything but 0
        };
        QString column;
        bool contains{false};    // the column name contains `column`, or is it
        bool ignoreCase{false};
        Role role{Value};
        QString channel;         // Value
        QString unit;            // of the values in the file ("mph"); converted to what channels store

        bool operator==(const Mapping &other) const
        {
            return column == other.column && contains == other.contains && ignoreCase == other.ignoreCase
                   && role == other.role && channel == other.channel && unit == other.unit;
        }
    };

    QString name;
    QVector<LineMatcher> matchers;
    QVector<int> headerLines{0};   // several: their names joined with " - "
    int dataStart{1};
    QString separators{","};
    bool decimalComma{false};
    QVector<Calculator> calculators;
    QVector<Mapping> mappings;

    // Whether the first lines of a file are of this kind. A profile without
    // matchers matches nothing.
    bool matches(const QStringList &lines) const;
    // The lines needed to tell.
    int linesToMatch() const;
};

class DataProfileFile
{
    Q_DECLARE_TR_FUNCTIONS(DataProfileFile)

public:
    static QByteArray toJson(const DataProfile &profile);
    static bool fromJson(const QByteArray &json, DataProfile *profile, QString *error = nullptr);
};
