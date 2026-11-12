#pragma once

#include "gauge.h"

#include <QCoreApplication>

// Converts a .veg gauge (.veg, JSON) into a native gauge.
//
// Reads the user's own files; nothing of the old editor is shipped with the
// program. Anything the native model cannot show is left out and listed in
// the warnings, and so are logos: the maker's trademark is not ours to draw.
class VegGaugeImporter
{
    Q_DECLARE_TR_FUNCTIONS(VegGaugeImporter)

public:
    struct Result
    {
        bool ok{false};
        Gauge gauge;
        QStringList warnings;   // parts left out, images not found
        QString error;          // when !ok
    };

    // Images are looked up relative to the file's folder, ignoring case
    // (the files were made on Windows).
    static Result importFile(const QString &vegPath);

    // imageFolder: where "images/..." paths start; empty = do not load images.
    static Result parse(const QByteArray &json, const QString &name, const QString &imageFolder);

    // the .veg format's name for a measurement -> our channel id ("speed2d" -> "speed").
    static QString channelFor(const QString &sourceType);
    // "____SML_VX_speed" -> "SML VX speed"
    static QString nameFromFileName(const QString &baseName);
};
