#pragma once

#include "../laps/lapengine.h"
#include "../overlay/overlay.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

// One video with its data and what is drawn on it. This is what a project
// file (.rtvproj, JSON) holds. Preferences of the program itself (units,
// colours, decoder) are not part of it - see AppSettings.
struct Project
{
    QString videoPath;        // absolute, or empty
    QString telemetryPath;    // absolute, or empty

    // telemetry time = video time + syncOffset
    double syncOffset{0.0};
    // false: the offset is whatever automatic sync finds; true: the user's value
    bool syncIsManual{false};

    // where laps begin; invalid = the laps the device recorded
    StartFinishLine startFinish;

    QVector<OverlayItem> overlay{Overlay::defaultItems()};
    GaugeTheme theme;         // colours of all gauges
};

class ProjectFile
{
    Q_DECLARE_TR_FUNCTIONS(ProjectFile)

public:
    static constexpr char Suffix[] = "rtvproj";

    // Files in the project's folder or below it are referred to relatively,
    // so a folder with the project, the video and the data can be moved or
    // copied as a whole; anything elsewhere keeps its full path. On loading,
    // a file that is not where it was is also looked for next to the project.
    static bool save(const QString &path, const Project &project, QString *error = nullptr);
    static bool load(const QString &path, Project *project, QString *error = nullptr);

    // The same, without touching the disk. projectDir is where relative
    // references start from.
    static QByteArray toJson(const Project &project, const QString &projectDir);
    static bool fromJson(const QByteArray &json, const QString &projectDir, Project *project, QString *error = nullptr);
};
