#pragma once

#include "project/project.h"
#include "settings/appsettings.h"
#include "telemetry/gears.h"
#include "telemetry/powerestimate.h"
#include "weather/weather.h"
#include "telemetry/telemetrysession.h"
#include "units/units.h"
#include "update/releasecheck.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMediaPlayer>

#include <functional>

class Playback;
class QAction;
class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QPushButton;
class QUndoStack;
class GaugeLibraryPanel;
class QTreeWidget;
class DataPanel;
class LapComparePanel;
class LapsPanel;
class MusicPanel;
class TimelinePanel;
class QVideoSink;
class VideoCanvas;

// Main window: preview canvas with transport and sync controls, plus the
// docks described in docs/planning/03-architecture.md. Docks without content
// yet are empty placeholders until their milestones land.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

public slots:
    // Replace the video / the data of the current project. Sync starts over.
    // shifts: breaks in a video joined from several recordings
    void openVideo(const QString &path, const QVector<TimeShift> &shifts = {});
    // Imports in a worker thread; the window stays usable meanwhile.
    void openTelemetry(const QString &path);
    void openProject(const QString &path);
    void openDropped(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Files dropped on the window: a project, a video, data, gauges and templates for the library.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void chooseVideo();
    // paths: the videos the dialog starts with
    void joinVideos(const QStringList &paths = {});
    // A video picked by the user: the chapters of a camera recording offered as one.
    void openChosenVideo(const QString &path);
    void chooseTelemetry();
    void chooseProject();
    // Both return false when the user backed out or writing failed.
    bool saveProject();
    bool saveProjectAs();
    void exportVideo();
    // Editing what is on the video. Every one of these is one undo step.
    void addGauge(const QString &gaugeId);
    // A video of another shape than the gauges were placed on: each placed again from its nearest
    // edges, as a template places them
    void fitLayoutTo(double aspect);
    // ... centered at `center` (fractions of the frame), kept on the picture; below 0: where addGauge puts it
    void addGaugeAt(const QString &gaugeId, const QPointF &center);
    void applyTemplate(const QString &templateId);
    void removeGauge(int index);
    void onItemGeometryEdited(int index, double x, double y, double height, double aspect);
    void setGaugeChannel(int index, const QString &channel);
    // A part of the gauge made for `own` shows `shown` instead.
    void setGaugePartChannel(int index, const QString &own, const QString &shown);
    void setGaugeScaleMax(int index, double max);
    void setGaugeStreets(int index, bool shown);
    // No video: the gauges over a background (a color, or see-through), as long as the data.
    void chooseBackground();
    void useBackground(const VideoBackground &background);
    void setGaugeCloseUp(int index, double meters, const GaugeComponent::SpeedZoom &zoom = {});
    // Asks for a close-up of its own, fixed or widening with the speed, and sets it.
    void askGaugeCloseUp(int index, bool withSpeed);
    // Asks how a gauge's 3D flyover is seen: the camera's tilt and height, the hills' height.
    void askFlyoverCamera(int index);
    // Works the data out again from the file's (corrections, terrain heights, what follows from them).
    void recomputeData();
    void saveAsTemplate();
    void chooseThemeColor(int role);
    // The colors and font the controls show and change: the selected gauge's own, or every gauge's.
    bool editsOwnLook() const;
    const GaugeTheme &shownTheme() const;
    void changeTheme(const QString &description, const std::function<void(GaugeTheme &)> &change);
    // The selected gauge's own look changed as `change` does.
    void changeGaugeLook(const QString &description, const std::function<void(OverlayItem &)> &change);
    void showItemMenu(int index, const QPoint &globalPosition);
    void updateProperties();
    void togglePlayback();
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onDurationChanged(qint64 durationMs);
    void onPositionChanged(qint64 positionMs);
    void onPlayerError(QMediaPlayer::Error error, const QString &message);
    void onOffsetEdited(double seconds);
    void autoSync();
    void showSettings();
    // Picks up what the settings dialog changed and can change without a restart.
    void applySettings();
    void applyMapSettings();
    void showAbout();
    // Newer releases: looked for quietly once a day, or at once from the Help menu.
    void checkForUpdates(bool asked);
    void showNewRelease();
    // Replaces this copy with `release` where it can, and starts it.
    void updateTo(const ReleaseCheck::Release &release);

private:
    // A File submenu of the files of `kind` opened lately; `open` opens one.
    QMenu *addRecentMenu(QMenu *parent, const QString &title, AppSettings::RecentKind kind,
                         const std::function<void(const QString &)> &open);
    QWidget *createCentralWidget();
    void createDocks();
    void createMenus();
    QDockWidget *addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area);
    // session is null when the import failed; error then says why.
    void telemetryImported(const QString &path, const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session,
                           const QString &error);
    void setDataEdits(const QVector<DataEdit> &edits, const QString &description);
    void editDataSources();
    void setDataSources(const QVector<DataSource> &sources, const QString &description);
    // A dock just floated or docked again: in sight, in front.
    void settleDock(QDockWidget *dock, bool floating);
    void loadVideo(const QString &path);
    // keepSync: the offset comes with the project and must survive the import
    void loadTelemetry(const QString &path, bool keepSync, bool quietly = false);
    void rebuildOverlay();
    QWidget *createPropertiesPanel();
    // The part of the project the undo stack covers.
    struct OverlayState
    {
        QVector<OverlayItem> items;
        GaugeTheme theme;
    };
    // Pushes an undo step that takes the overlay from what it is now to `after`.
    void changeOverlay(const QString &description, const OverlayState &after);
    void applyOverlayState(const OverlayState &state);
    // Channels a gauge can be bound to: those of the recording, the current one included.
    QStringList channelChoices(const QString &current) const;
    // Asks about unsaved changes. False: the user wants to stay.
    bool maybeSave();
    void setModified(bool modified);
    void updateWindowTitle();
    void setSyncOffset(double seconds, const QString &explanation);
    void showSyncExplanation(const QString &explanation);
    // the data panel's picked stretch (data time) as a cut of the video
    void cutVideoOfData(double start, double end);
    // File > Save data as...: the data with its corrections, GPX or CSV
    void saveData();
    void updateSourcesInfo();
    // Fills the "now" column of the channel table for the frame on screen.
    // Laps from the project's start/finish line on the recorded session; m_session follows.
    void updateLaps();
    // A line across the route near a click on the map; invalid (and said why) when off it.
    StartFinishLine lineNear(double lat, double lon, const QString &offRoute);
    void placeStartFinish(double lat, double lon);
    void placeSector(double lat, double lon);
    void markSurface(double fromLat, double fromLon, double toLat, double toLon, const QString &surface, double factor);
    // the stretch start..end (data time) ridden on `surface`
    void markSurfaceBetween(double start, double end, const QString &surface, double factor);
    void setSurfaces(const QVector<SurfaceStretch> &surfaces, const QString &description);
    void setCuts(const QVector<TimeRange> &cuts, const QString &description);
    void setSoundtrack(const Soundtrack &soundtrack, const QString &description);
    // The music player at the place, track and volume that belong to the video's moment.
    void syncMusic();
    void setLapLines(const StartFinishLine &line, const QVector<StartFinishLine> &sectors, const QString &description);
    void seekTelemetryTime(double time);
    void updateChannelValues();
    void updateTimeLabel();

    QMediaPlayer *m_player{nullptr};
    Playback *m_playback{nullptr};
    QAudioOutput *m_audioOutput{nullptr};
    QVideoSink *m_videoSink{nullptr};
    // the music under the video, in step with it
    QMediaPlayer *m_musicPlayer{nullptr};
    QAudioOutput *m_musicOutput{nullptr};
    QString m_musicSource;
    bool m_musicSwitching{false};
    QMediaPlayer::MediaStatus m_musicWaitedFor{QMediaPlayer::NoMedia};   // said once in the log
    // The video and the music player now, for the Diagnostics dock.
    QString playersState() const;   // the music player is being given another file
    // where the music was wanted at the last look, and how long ago that was
    int m_musicTrack{-1};
    qint64 m_musicWanted{0};
    QElapsedTimer m_musicClock;
    // what is heard: the project's, or volumes still being dragged
    Soundtrack m_heardSoundtrack;

    VideoCanvas *m_canvas{nullptr};
    QToolButton *m_playButton{nullptr};
    QSlider *m_seekSlider{nullptr};
    QLabel *m_timeLabel{nullptr};
    QDoubleSpinBox *m_offsetSpin{nullptr};
    QDoubleSpinBox *m_rateSpin{nullptr};
    QToolButton *m_autoSyncButton{nullptr};
    QLabel *m_sourcesLabel{nullptr};
    QComboBox *m_elevationCombo{nullptr};
    // heights of the terrain were missing when the data was last worked out
    bool m_terrainWaiting{false};
    QTreeWidget *m_channelTree{nullptr};
    QAction *m_exportAction{nullptr};
    QAction *m_dataSourcesAction{nullptr};
    QAction *m_saveDataAction{nullptr};
    GaugeLibraryPanel *m_libraryPanel{nullptr};
    LapsPanel *m_lapsPanel{nullptr};
    TimelinePanel *m_timelinePanel{nullptr};
    QDockWidget *m_compareDock{nullptr};
    LapComparePanel *m_comparePanel{nullptr};
    QDockWidget *m_dataDock{nullptr};
    DataPanel *m_dataPanel{nullptr};
    QDockWidget *m_lapsDock{nullptr};
    QDockWidget *m_musicDock{nullptr};
    QDockWidget *m_diagnosticsDock{nullptr};
    MusicPanel *m_musicPanel{nullptr};
    QComboBox *m_channelCombo{nullptr};
    // what the parts of the selected gauge with channels of their own show: rows made anew for each
    // gauge in a widget of their own, which goes when the next is made (a choice in it may be under way)
    QWidget *m_partsHolder{nullptr};
    QWidget *m_parts{nullptr};
    QLabel *m_scaleMaxLabel{nullptr};
    QDoubleSpinBox *m_scaleMaxSpin{nullptr};
    QPushButton *m_removeGaugeButton{nullptr};
    // the selected gauge's own look; with its own colors and font the controls under them edit those
    QSpinBox *m_textSizeSpin{nullptr};
    QSpinBox *m_opacitySpin{nullptr};
    QCheckBox *m_ownLookCheck{nullptr};
    QLabel *m_lookTitle{nullptr};
    QVector<QPushButton *> m_themeButtons;
    QComboBox *m_fontCombo{nullptr};
    QComboBox *m_fontWeightCombo{nullptr};
    QCheckBox *m_fontEverywhereCheck{nullptr};
    QUndoStack *m_undoStack{nullptr};
    ReleaseCheck *m_releaseCheck{nullptr};
    ReleaseCheck::Release m_newRelease;     // a newer one found; invalid when none
    bool m_updateAsked{false};              // the check under way was asked for: its answer is told
    QLabel *m_updateLabel{nullptr};         // in the status bar while there is a newer one
    QAction *m_updateAction{nullptr};
    // The project differs from what is on disk in a way worth asking about.
    bool m_modified{false};

    QDockWidget *m_sourcesDock{nullptr};
    QDockWidget *m_libraryDock{nullptr};
    QDockWidget *m_propertiesDock{nullptr};
    QDockWidget *m_timelineDock{nullptr};

    Project m_project;
    QString m_projectPath;   // empty until saved or opened
    // Import in progress; a result for any other path is stale and dropped.
    QString m_pendingTelemetryPath;
    bool m_pendingKeepsSync{false};
    bool m_pendingRecentData{false};  // the data being read goes among the recent ones
    bool m_pendingQuietly{false};   // the video's own data, tried: no message when it has none
    quint64 m_importGeneration{0};   // the load whose result is wanted
    // terrain or weather arriving: the data is worked out again when they stop coming
    QTimer *m_arrivalTimer{nullptr};
    QElapsedTimer m_sinceRecompute;
    TelemetrySessionPtr m_session;          // with the laps of the project's start/finish line
    TelemetrySessionPtr m_rawSession;       // as the files gave it: the main one with the channels added
    TelemetrySessionPtr m_mainSession;      // the main file alone
    TelemetrySessionPtr m_recordedSession;  // as the file gave it (and what could be worked out)
    Units m_units;
    PowerEstimate::Rider m_rider;
    QByteArray m_defaultLayout;
    // A moment of the data asked for that the video cannot show (no video, or before or after it):
    // shown in the panels until the video moves away from where it was then.
    double m_heldTime{-1.0};
    qint64 m_heldVideoPosition{-1};
    double shownTelemetryTime() const;
    WeatherChoice m_weather;
    Drivetrain m_drivetrain;
    // the weather of the ride was asked for when the data was worked out, and has not come yet
    bool m_weatherWaiting{false};
    QComboBox *m_windCombo{nullptr};
    QDoubleSpinBox *m_windSpeedSpin{nullptr};
    QDoubleSpinBox *m_windFromSpin{nullptr};
    // Shows the project's wind in the Sources dock, in the units of the settings.
    void showWind();
};
