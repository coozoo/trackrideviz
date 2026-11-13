#pragma once

#include "dataprofile.h"

#include <QCoreApplication>

// Converts an XML data profile (<DataProfile version="2">) into a native one.
//
// Unit conversions, sums and products, gap filling, smoothing and deltas are
// kept as calculators. Calculators that derive measurements (lap statistics,
// ascent, heading and distance from positions, G from speed) are left out
// and listed: those are worked out for every recording alike, whatever its
// source. Positions projected to feet or metres are left out too; ours stay
// in degrees.
class XmlDataProfileImporter
{
    Q_DECLARE_TR_FUNCTIONS(XmlDataProfileImporter)

public:
    struct Result
    {
        bool ok{false};
        DataProfile profile;
        QStringList warnings;
        QString error;
    };

    // The profile is named after the file.
    static Result importFile(const QString &path);
    static Result parse(const QByteArray &xml, const QString &name);
    // Whether a file holds a data profile (it only looks at the start).
    static bool isDataProfile(const QString &path);
};
