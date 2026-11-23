#pragma once

#include "../telemetry/datasources.h"
#include "../telemetry/surfaces.h"
#include "../weather/weather.h"
#include "../maps/terrain.h"
#include "../laps/lapengine.h"
#include "../overlay/overlay.h"
#include "../telemetry/dataedits.h"
#include "../sync/syncmap.h"
#include "../video/cutlist.h"
#include "../video/soundtrack.h"
#include "../video/videobackground.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

// One video with its data and what is drawn on it. This is what a project
// file (.rtvproj, JSON) holds. Preferences of the program itself (units,
// colors, decoder) are not part of it - see AppSettings.
struct Project
{
    QString videoPath;        // absolute, or empty
    // without a video: the gauges over this, as long as the data (see VideoBackground)
    VideoBackground background;
    bool hasPicture() const { return !videoPath.isEmpty() || background.isOn(); }
    QString telemetryPath;    // absolute, or empty
    // files whose channels are added to it, such as a heart-rate strap's
    QVector<DataSource> moreData;

    // telemetry time = video time x syncRate + syncOffset
    double syncOffset{0.0};
    // a time-lapse video: seconds of the data in one of its seconds; 1 = an ordinary video
    double syncRate{1.0};
    // false: the offset is whatever automatic sync finds; true: the user's value
    bool syncIsManual{false};
    // a video joined from recordings with breaks between them: each part's shift
    QVector<TimeShift> videoShifts;

    // without a video the data plays on its own clock: no rate
    SyncMap sync() const { return SyncMap(syncOffset, videoShifts, videoPath.isEmpty() ? 1.0 : syncRate); }

    // where laps begin; invalid = the laps the device recorded
    StartFinishLine startFinish;
    // lines inside the lap that split it into sectors, in any order
    QVector<StartFinishLine> sectors;

    // parts of the video left out of the export, video time
    QVector<TimeRange> cuts;
    // music under the video, and the volumes
    Soundtrack soundtrack;
    // corrections of the data, applied before anything is worked out from it
    QVector<DataEdit> dataEdits;
    // a wind entered by hand, in place of the weather history's
    HandWind wind;
    // stretches of the ride marked as a surface, for the power estimate
    QVector<SurfaceStretch> surfaces;
    // the heights of a terrain model as the elevation: when the recording has none, always, never
    Terrain::Use elevation{Terrain::WhenMissing};

    QVector<OverlayItem> overlay{Overlay::defaultItems()};
    // The shape of the picture (width / height) the gauges' places were set on. A video of
    // another shape gets them placed again from their nearest edges; 0 = not known (older
    // projects), the first video's shape is taken then.
    double layoutAspect{16.0 / 9.0};
    GaugeTheme theme;         // colors of all gauges
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
