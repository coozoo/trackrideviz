#pragma once

#include "gauge.h"

#include <QCoreApplication>

// Converts an XML gauge (a folder holding gauge.xml and its pictures, file
// version 3) into a native gauge.
//
// These gauges are drawn rather than painted: their parts are shapes, scales,
// needles and texts, which become Shape, Dial, Tape and Text components.
// A gauge is made for a measurement in a fixed unit (a dial printed 0-200
// mph); the parts keep that unit, so the needle matches the numbers whatever
// the display units are. Anything the native model cannot show is left out
// and listed in the warnings.
class XmlGaugeImporter
{
    Q_DECLARE_TR_FUNCTIONS(XmlGaugeImporter)

public:
    static constexpr char FileName[] = "gauge.xml";

    struct Result
    {
        bool ok{false};
        Gauge gauge;
        QStringList warnings;   // parts left out, images not found
        QString error;          // when !ok
    };

    // The gauge is named after the folder.
    static Result importFolder(const QString &folder);

    // imageFolder: where the pictures are; empty = do not load images.
    static Result parse(const QByteArray &xml, const QString &name, const QString &imageFolder);

    // A data type of these files ("Speed (MPH)") -> our channel id ("speed")
    // and the unit it is measured in ("mph"; empty when it has none).
    static QString channelFor(const QString &dataType, QString *unit = nullptr);
};
