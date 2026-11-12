#pragma once

#include "gaugetemplate.h"

#include <QCoreApplication>
#include <QVector>

#include <functional>

// The user's templates, one file each under userFolder(). The gauges they
// use live in the GaugeLibrary; a template only names them.
//
// Templates are named by id: "template:<file name without .json>".
class TemplateLibrary
{
    Q_DECLARE_TR_FUNCTIONS(TemplateLibrary)

public:
    struct Entry
    {
        QString id;
        QString name;
        QString category;
    };

    // <application data>/templates unless overridden (tests).
    static QString userFolder();
    static void setUserFolder(const QString &folder);

    // In alphabetical order of their names.
    static QVector<Entry> entries();
    // An empty template (no items) when there is none.
    static GaugeTemplate get(const QString &id);
    // Stores a template and returns its id; one of the same name is replaced.
    static QString add(const GaugeTemplate &gaugeTemplate, QString *error = nullptr);
    static bool remove(const QString &id);

    struct ImportSummary
    {
        int imported{0};
        int gaugesImported{0};  // brought in along the way, because a template uses them
        QStringList failed;     // "file: reason"
        QStringList notes;      // "template: what was left out"
    };
    // Converts .vet templates (.vet). Gauges they use that are not in
    // the gauge library yet are imported from `widgetsFolder` (the old editor's
    // "Widgets"). Blocking: call from a worker thread, then
    // GaugeLibrary::refresh() on the GUI thread.
    static ImportSummary importVet(const QStringList &vetFiles, const QString &widgetsFolder);
};

// Converts one .vet template. `findGauge` tells, for a gauge file name
// the template mentions, the id and design height of that gauge in the
// library; an empty id when it is not there.
class VetTemplateImporter
{
    Q_DECLARE_TR_FUNCTIONS(VetTemplateImporter)

public:
    struct Result
    {
        bool ok{false};
        GaugeTemplate gaugeTemplate;
        QStringList missingGauges;   // file names of gauges the template uses but the library lacks
        QString error;
    };
    struct FoundGauge
    {
        QString id;
        double height{0.0};
        QStringList supportedTypes;
    };
    static Result parse(const QByteArray &json, const QString &name,
                        const std::function<FoundGauge(const QString &fileName)> &findGauge);
    // The lookup the program uses: the gauge library, by the name an imported gauge gets.
    static FoundGauge findInLibrary(const QString &fileName);
    // The file names of the gauges a template uses.
    static QStringList gaugeFiles(const QByteArray &json);
    // Theme of a the old editor template: its three colours; text white, black on a light background.
    static GaugeTheme themeOf(const QColor &background, const QColor &accent, const QColor &accent2);
};
