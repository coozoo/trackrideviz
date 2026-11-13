#pragma once

#include "gaugetemplate.h"

#include <QCoreApplication>

#include <functional>

// Converts an XML template: a folder holding project.cdp, which places
// gauges by name in pixels of the video, and a Gauges folder with a copy of
// each (XML gauges, see XmlGaugeImporter).
//
// The files do not say which frame size they were laid out for. A size in
// the template's name ("720p", "1080p") decides; otherwise the smallest of
// 720 x 480, 1280 x 720 and 1920 x 1080 that holds every gauge.
class XmlTemplateImporter
{
    Q_DECLARE_TR_FUNCTIONS(XmlTemplateImporter)

public:
    static constexpr char FileName[] = "project.cdp";

    struct FoundGauge
    {
        QString id;              // empty: not in the library
        QSizeF size;             // design size
        QStringList supportedTypes;
    };
    struct Result
    {
        bool ok{false};
        GaugeTemplate gaugeTemplate;
        QSize frameSize;             // what the layout was made for
        QStringList missingGauges;   // names the library lacks
        QStringList notes;
        QString error;
    };

    static Result parse(const QByteArray &xml, const QString &name,
                        const std::function<FoundGauge(const QString &gaugeName)> &findGauge);
    // The lookup the program uses: the gauge library, by name.
    static FoundGauge findInLibrary(const QString &gaugeName);
    // The names of the gauges a template places.
    static QStringList gaugeNames(const QByteArray &xml);
};
