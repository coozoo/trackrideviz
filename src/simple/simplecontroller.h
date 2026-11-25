#pragma once

#include "overlay/overlay.h"
#include "project/project.h"
#include "video/mediainfo.h"
#include "video/videoencoders.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>

#include <memory>

class ExportJob;
class QThread;

// What the simplified interface does, for its QML: a video and its data, put in step by the
// clocks, a template over it and an export with a profile; the rest automatic. The same core
// the full window and the command line use, without their choices.
class SimpleController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canGoBack READ canGoBack CONSTANT)
    Q_PROPERTY(QUrl videoUrl READ videoUrl NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(QString dataName READ dataName NOTIFY dataChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(double syncOffset READ syncOffset NOTIFY syncChanged)
    Q_PROPERTY(QString syncText READ syncText NOTIFY syncChanged)
    Q_PROPERTY(QStringList templateNames READ templateNames CONSTANT)
    Q_PROPERTY(QStringList templateIds READ templateIds CONSTANT)
    Q_PROPERTY(QStringList gaugeNames READ gaugeNames CONSTANT)
    Q_PROPERTY(QStringList gaugeIds READ gaugeIds CONSTANT)
    Q_PROPERTY(int gaugeCount READ gaugeCount NOTIFY overlayChanged)
    Q_PROPERTY(QStringList musicNames READ musicNames NOTIFY musicChanged)
    Q_PROPERTY(int videoVolume READ videoVolume WRITE setVideoVolume NOTIFY musicChanged)
    Q_PROPERTY(int musicVolume READ musicVolume WRITE setMusicVolume NOTIFY musicChanged)
    Q_PROPERTY(int templateIndex READ templateIndex NOTIFY overlayChanged)
    Q_PROPERTY(QStringList profileNames READ profileNames CONSTANT)
    Q_PROPERTY(int profileIndex READ profileIndex WRITE setProfileIndex NOTIFY profileChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY videoChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportText READ exportText NOTIFY exportChanged)
    Q_PROPERTY(QUrl exportedUrl READ exportedUrl NOTIFY exportChanged)
    Q_PROPERTY(bool isDemo READ isDemo NOTIFY dataChanged)
    // without a video: how long the picture over the data runs, ms
    Q_PROPERTY(qint64 dataDuration READ dataDuration NOTIFY dataChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY projectChanged)
    Q_PROPERTY(QUrl projectFolder READ projectFolder NOTIFY projectChanged)
    // the laps: number, start (video ms), time, delta (text), best, reference
    Q_PROPERTY(QVariantList laps READ laps NOTIFY lapsChanged)
    Q_PROPERTY(bool hasLine READ hasLine NOTIFY lapsChanged)
    // what the data has, for the graph under the video
    Q_PROPERTY(QStringList channelIds READ channelIds NOTIFY dataChanged)
    Q_PROPERTY(QStringList channelNames READ channelNames NOTIFY dataChanged)
    // opened lately, files that are still there: url, name, folder, kind ("project", "video", "data")
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentChanged)

public:
    explicit SimpleController(bool canGoBack, QObject *parent = nullptr);
    ~SimpleController() override;

    bool canGoBack() const { return m_canGoBack; }
    QUrl videoUrl() const;
    QString videoName() const;
    QString dataName() const;
    bool hasData() const { return bool(m_session); }
    bool busy() const { return m_loading > 0; }
    QString message() const { return m_message; }
    double syncOffset() const { return m_project.syncOffset; }
    QString syncText() const { return m_syncText; }
    QStringList templateNames() const;
    QStringList templateIds() const;
    QStringList gaugeNames() const;
    QStringList gaugeIds() const;
    int gaugeCount() const { return int(m_project.overlay.size()); }
    QStringList musicNames() const;
    int videoVolume() const { return m_project.soundtrack.videoVolume; }
    void setVideoVolume(int percent);
    int musicVolume() const { return m_project.soundtrack.musicVolume; }
    void setMusicVolume(int percent);
    int templateIndex() const { return m_templateIndex; }
    QStringList profileNames() const;
    int profileIndex() const { return m_profileIndex; }
    void setProfileIndex(int index);
    double aspect() const;
    bool exporting() const { return m_thread != nullptr; }
    double exportProgress() const { return m_exportProgress; }
    QString exportText() const { return m_exportText; }
    QUrl exportedUrl() const;
    bool isDemo() const { return m_demo; }
    qint64 dataDuration() const;
    QString projectName() const;
    bool modified() const { return m_modified; }
    bool canSave() const { return !m_demo && (!m_project.videoPath.isEmpty() || !m_project.telemetryPath.isEmpty()); }
    QUrl projectFolder() const;
    QVariantList laps() const;
    bool hasLine() const { return m_project.startFinish.isValid(); }
    QStringList channelIds() const;
    QStringList channelNames() const;
    QVariantList recentFiles() const;
    // The data as worked out, its laps found; null without any.
    TelemetrySessionPtr session() const { return m_session; }

    // What it works on, all of it: given to the full window when it goes back.
    const Project &project() const { return m_project; }
    // The full window's project: its video, data, gauges, step and music; `path` its file, if saved.
    void openProject(const Project &project, const QString &path = QString());
    QString projectPath() const { return m_projectPath; }

    // Whatever they are: a project (then only it), a video, data, music.
    Q_INVOKABLE void openFiles(const QList<QUrl> &urls);
    // The made-up ride, for a look before one's own is there.
    Q_INVOKABLE void openDemo();
    // Into its file (false: none yet, or it could not be written), or into this one.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &url);
    // Everything closed: the start again.
    Q_INVOKABLE void newProject();

    // What is drawn over the picture, for GaugeOverlay; never null.
    std::shared_ptr<const Overlay> overlay() const { return m_overlay; }
    // The data's time at a moment of the video.
    Q_INVOKABLE double telemetryTime(qint64 videoMs) const;

    // A video; its data is read out of it when it has some and none is open.
    Q_INVOKABLE void openVideo(const QUrl &url);
    // Data from a file of its own (FIT, GPX, CSV...).
    Q_INVOKABLE void openData(const QUrl &url);
    // The data moved against the video by this many seconds (+: later).
    Q_INVOKABLE void nudgeSync(double seconds);
    // In step by the clocks of the camera and the data again.
    Q_INVOKABLE void syncByClocks();
    Q_INVOKABLE void chooseTemplate(int index);
    // The data's offset typed in: seconds, + when the gauges are late.
    Q_INVOKABLE void setSyncOffset(double seconds);

    // The gauges, on a picture `width` x `height` pixels: which is at a point (-1: none), where one is.
    Q_INVOKABLE int gaugeAt(double x, double y, double width, double height) const;
    Q_INVOKABLE QRectF gaugeRect(int index, double width, double height) const;
    // Moved so that its top left is at (x, y), fractions of the picture; `done` at the end of a drag
    // (before it, only what is drawn follows).
    Q_INVOKABLE void moveGauge(int index, double x, double y, bool done);
    // Its rectangle, fractions of the picture, as a corner is dragged; a gauge that keeps its shape
    // takes the height and its own width.
    Q_INVOKABLE void setGaugeRect(int index, double x, double y, double width, double height, bool done);
    // Its height, a fraction of the picture's, about its middle.
    Q_INVOKABLE void resizeGauge(int index, double height);
    Q_INVOKABLE void addGauge(const QString &gaugeId);
    Q_INVOKABLE void removeGauge(int index);
    // What the properties of a gauge show: name, channel, channelIds, channelNames, height,
    // width, keepShape, textScale, opacity, hasScale, scaleMax, unit, units, ownColors, colors
    // (text, background, accent, 2nd accent, as shown); and as the full
    // window's menu of a gauge has them: hasGraph, byDistance; hasRoute, streets, mapIds,
    // mapNames, map, closeUp, bySpeed, far, slow, fast, speedUnit, canBySpeed, headingUp,
    // flyover, ghost, tilt, cameraHeight, hills.
    Q_INVOKABLE QVariantMap gaugeProperties(int index) const;
    Q_INVOKABLE void setGaugeChannel(int index, const QString &channel);
    Q_INVOKABLE void setGaugeTextScale(int index, double scale);
    Q_INVOKABLE void setGaugeOpacity(int index, double opacity);
    Q_INVOKABLE void setGaugeScaleMax(int index, double max);
    // 0: as in the settings, 1: metric, 2: imperial
    Q_INVOKABLE void setGaugeUnits(int index, int units);
    // Its colors its own, starting from those of all gauges; or as all the others.
    Q_INVOKABLE void setGaugeOwnColors(int index, bool own);
    // A color of its own (with them on) or of all gauges: 0 text, 1 background, 2 accent, 3 2nd accent.
    Q_INVOKABLE void setGaugeColor(int index, int role, const QColor &color);
    // Kept, its width follows its height; set free, `width` (a fraction of the picture's) is its own.
    Q_INVOKABLE void setGaugeKeepShape(int index, bool keep);
    Q_INVOKABLE void setGaugeWidth(int index, double width);
    Q_INVOKABLE void setGaugeByDistance(int index, bool on);
    Q_INVOKABLE void setGaugeStreets(int index, bool shown);
    // A map provider's id; empty: as in the settings.
    Q_INVOKABLE void setGaugeMap(int index, const QString &map);
    // `meters` across, 0: the whole route; with `far` above 0 wider with the speed: `meters` at
    // `slow` or slower, `far` at `fast` or faster (in the gauge's units). False when the speeds
    // are not one above the other.
    Q_INVOKABLE bool setGaugeCloseUp(int index, double meters, double far, double slow, double fast);
    Q_INVOKABLE void setGaugeHeadingUp(int index, bool up);
    Q_INVOKABLE void setGaugeFlyover(int index, bool on);
    Q_INVOKABLE void setGaugeGhost(int index, bool on);
    // Degrees from straight down, meters above the rider, hills this many times higher; tilt 0:
    // as the gauge has it.
    Q_INVOKABLE void setGaugeFlyoverCamera(int index, double tilt, double height, double hills);

    Q_INVOKABLE void addMusic(const QUrl &url);
    Q_INVOKABLE void removeMusic(int index);
    // What the music plays at a moment of the video: url (empty: nothing), position (ms), volume (0..1).
    Q_INVOKABLE QVariantMap musicAt(qint64 videoMs) const;
    // The start/finish line where the route is at this moment of the video; false without a
    // direction of travel there.
    Q_INVOKABLE bool lineHere(qint64 videoMs);
    // ... where the route passes nearest to a place on the map; false when it is well off the route
    // or the direction of travel is not known there.
    Q_INVOKABLE bool placeLine(double lat, double lon);
    // ... moved to where nearby it gives the most laps; false when no place does.
    Q_INVOKABLE bool findBestLine();
    // No line: the laps the device recorded.
    Q_INVOKABLE void clearLine();
    // The lap the others are measured against and the ghost rides, by its number; 0: the fastest.
    Q_INVOKABLE void setReferenceLap(int number);
    Q_INVOKABLE void startExport();
    // Only from `from` to `to`, seconds into the video.
    Q_INVOKABLE void startExportRange(double from, double to);
    // Only that lap of the video.
    Q_INVOKABLE void exportLap(int number);
    Q_INVOKABLE void stopExport();
    // Back to the full window (only when it was left for this).
    Q_INVOKABLE void goBack();

signals:
    void videoChanged();
    void dataChanged();
    void busyChanged();
    void messageChanged();
    void syncChanged();
    void overlayChanged();
    void profileChanged();
    void exportChanged();
    void musicChanged();
    void backRequested();
    void projectChanged();
    void lapsChanged();
    void recentChanged();

private:
    void setMessage(const QString &message);
    void setBusy(bool more);
    void loadData(const QString &path, bool fromVideo);
    void dataLoaded(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &worked, const TelemetrySessionPtr &session,
                    const QString &path, bool fromVideo,
                    const QString &error, bool waiting);
    void rebuildOverlay();
    // m_project.overlay[index] changed: drawn again
    void changeGauge(int index, const std::function<void(OverlayItem &)> &change);
    QRectF pictureFrame(double width, double height) const { return QRectF(0.0, 0.0, width, height); }
    void setSync(double offset, const QString &text);
    // in step by the clocks, unless it was put in step by hand
    void followClocks();
    void setModified(bool modified = true);
    // the gauges changed by the user: to be saved
    void byHand();
    void applyTemplate(int index);
    // the laps found again, after their line or the reference changed
    void applyLaps();
    // without a video: the picture over the data, as long as it; with one, none
    void useBackground();
    // the gauges a ride without a video starts with: the route in 3D, the whole picture
    QVector<OverlayItem> flyoverItems() const;
    // the first gauges for the picture there is, unless the user's own are there; with a video,
    // the route in 3D under them taken away
    void firstGauges();
    // the export of m_exportStart for m_exportLength (0: to the end)
    void runExport();
    void probeVideo(const QString &path);
    void exportFinished(int outcome, const QString &message);

    const bool m_canGoBack;
    Project m_project;
    MediaInfo m_info;
    bool m_probed{false};
    TelemetrySessionPtr m_raw;
    TelemetrySessionPtr m_worked;   // m_raw worked out, before its laps
    TelemetrySessionPtr m_session;
    bool m_demo{false};
    QString m_projectPath;
    bool m_modified{false};
    std::shared_ptr<const Overlay> m_overlay;
    int m_templateIndex{-1};
    int m_profileIndex{0};
    int m_loading{0};
    quint64 m_dataGeneration{0};
    bool m_fileDataComing{false};   // data of a file of its own being read: none is looked for in the video
    QString m_message;
    QString m_syncText;
    // the heights of the terrain and the weather, while they come
    QTimer m_arrivals;
    int m_arrivalTries{0};

    // the encoders of this computer, found once
    bool m_encodersFound{false};
    VideoEncoders::ForEngine m_encoders;
    QThread *m_thread{nullptr};
    QPointer<ExportJob> m_job;     // on m_thread
    bool m_exportWaitsForEncoders{false};
    double m_exportStart{0.0};
    double m_exportLength{0.0};
    QString m_exportName;           // put after the video's name in the file's: a lap's
    double m_exportProgress{0.0};
    QString m_exportText;
    QString m_exportedPath;
    QString m_runningPath;
};
