#pragma once

#include "gauge.h"

#include <QCoreApplication>
#include <QVector>

// The gauges available on this computer: the ones drawn in code, the ones
// that come with the program (folders under BundledData::gaugesFolder()) and
// the user's own, each kept as a folder (see GaugeFile) under userFolder().
//
// Gauges are named by id in projects: "builtin:<name>", "bundled:<folder>"
// or "library:<folder>". Only the user's own can be changed or removed.
class GaugeLibrary
{
    Q_DECLARE_TR_FUNCTIONS(GaugeLibrary)

public:
    struct Entry
    {
        QString id;
        QString name;
        bool builtin{false};     // comes with the program: built in or bundled
    };

    // <application data>/gauges unless overridden (tests).
    static QString userFolder();
    static void setUserFolder(const QString &folder);

    // Built-in first, then the bundled and the user's, each in alphabetical order.
    static QVector<Entry> entries();

    // The gauge for an id; an empty gauge when there is none. Loaded once
    // and kept; call refresh() after the folder changed.
    static Gauge gauge(const QString &id);
    static void refresh();

    // The id a gauge stored under this name gets (whether it exists or not).
    static QString idForName(const QString &name);
    // The id of the stored gauge of this name, ignoring case (names come from
    // files made on Windows); empty when there is none.
    static QString findByName(const QString &name);

    // Stores a gauge under the user's folder and returns its id. An existing
    // gauge of that folder name is replaced. Empty on failure.
    static QString add(const Gauge &gauge, const QString &folderName, QString *error = nullptr);
    static bool remove(const QString &id);

    struct ImportSummary
    {
        int imported{0};
        int incomplete{0};      // imported, but with parts left out
        QStringList failed;     // "file: reason"
        QStringList notes;      // "gauge: what was left out"
    };
    // Converts .veg gauge files (.veg) into the library. Blocking: call
    // from a worker thread, then refresh() on the GUI thread.
    static ImportSummary importVeg(const QStringList &vegFiles);
};
