#pragma once

#include "dataprofile.h"

#include <QCoreApplication>

// The user's data profiles, one JSON file each under userFolder(). CSV files
// are read with the first one that recognizes them, in alphabetical order.
class DataProfileLibrary
{
    Q_DECLARE_TR_FUNCTIONS(DataProfileLibrary)

public:
    // <application data>/dataprofiles unless overridden (tests).
    static QString userFolder();
    static void setUserFolder(const QString &folder);

    // The user's, then those that come with the program (one of the user's of the same name wins).
    static QVector<DataProfile> profiles();
    static QVector<DataProfile> builtinProfiles();
    // Stores a profile; one of the same name is replaced. False on failure.
    static bool add(const DataProfile &profile, QString *error = nullptr);
    static bool remove(const QString &name);

    struct ImportSummary
    {
        int imported{0};
        QStringList failed;     // "file: reason"
        QStringList notes;      // "profile: what was left out"
    };
    // Converts XML data profiles into the library.
    static ImportSummary importXml(const QStringList &files);
};
