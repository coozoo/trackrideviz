#pragma once

#include "project/project.h"
#include "telemetry/powerestimate.h"
#include "weather/weather.h"
#include "telemetry/telemetrysession.h"
#include "units/units.h"

#include <QMainWindow>
#include <QMediaPlayer>

class QAction;
class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QToolButton;
class QPushButton;
class QUndoStack;
class GaugeLibraryPanel;
class QTreeWidget;
class DataPanel;
class LapComparePanel;
class LapsPanel;
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

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseVideo();
    void joinVideos();
    void chooseTelemetry();
    void chooseProject();
    // Both return false when the user backed out or writing failed.
    bool saveProject();
    bool saveProjectAs();
    void exportVideo();
    // Editing what is on the video. Every one of these is one undo step.
    void addGauge(const QString &gaugeId);
    void applyTemplate(const QString &templateId);
    void removeGauge(int index);
    void onItemGeometryEdited(int index, double x, double y, double height);
    void setGaugeChannel(int index, const QString &channel);
    void setGaugeStreets(int index, bool shown);
    void setGaugeCloseUp(int index, double meters);
    // Works the data out again from the file's (corrections, terrain heights, what follows from them).
    void recomputeData();
    void saveAsTemplate();
    void chooseThemeColor(int role);
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

private:
    QWidget *createCentralWidget();
    void createDocks();
    void createMenus();
    QDockWidget *addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area);
    // session is null when the import failed; error then says why.
    void telemetryImported(const QString &path, const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session,
                           const QString &error);
    void setDataEdits(const QVector<DataEdit> &edits, const QString &description);
    void loadVideo(const QString &path);
    // keepSync: the offset comes with the project and must survive the import
    void loadTelemetry(const QString &path, bool keepSync);
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
    void updateSourcesInfo();
    // Fills the "now" column of the channel table for the frame on screen.
    // Laps from the project's start/finish line on the recorded session; m_session follows.
    void updateLaps();
    // A line across the route near a click on the map; invalid (and said why) when off it.
    StartFinishLine lineNear(double lat, double lon, const QString &offRoute);
    void placeStartFinish(double lat, double lon);
    void placeSector(double lat, double lon);
    void setCuts(const QVector<TimeRange> &cuts, const QString &description);
    void setLapLines(const StartFinishLine &line, const QVector<StartFinishLine> &sectors, const QString &description);
    void seekTelemetryTime(double time);
    void updateChannelValues();
    void updateTimeLabel();

    QMediaPlayer *m_player{nullptr};
    QAudioOutput *m_audioOutput{nullptr};
    QVideoSink *m_videoSink{nullptr};

    VideoCanvas *m_canvas{nullptr};
    QToolButton *m_playButton{nullptr};
    QSlider *m_seekSlider{nullptr};
    QLabel *m_timeLabel{nullptr};
    QDoubleSpinBox *m_offsetSpin{nullptr};
    QToolButton *m_autoSyncButton{nullptr};
    QLabel *m_syncLabel{nullptr};
    QLabel *m_sourcesLabel{nullptr};
    QComboBox *m_elevationCombo{nullptr};
    // heights of the terrain were missing when the data was last worked out
    bool m_terrainWaiting{false};
    QTreeWidget *m_channelTree{nullptr};
    QAction *m_exportAction{nullptr};
    GaugeLibraryPanel *m_libraryPanel{nullptr};
    LapsPanel *m_lapsPanel{nullptr};
    TimelinePanel *m_timelinePanel{nullptr};
    QDockWidget *m_compareDock{nullptr};
    LapComparePanel *m_comparePanel{nullptr};
    QDockWidget *m_dataDock{nullptr};
    DataPanel *m_dataPanel{nullptr};
    QDockWidget *m_lapsDock{nullptr};
    QComboBox *m_channelCombo{nullptr};
    QPushButton *m_removeGaugeButton{nullptr};
    QVector<QPushButton *> m_themeButtons;
    QComboBox *m_fontCombo{nullptr};
    QComboBox *m_fontWeightCombo{nullptr};
    QCheckBox *m_fontEverywhereCheck{nullptr};
    QUndoStack *m_undoStack{nullptr};
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
    TelemetrySessionPtr m_session;          // with the laps of the project's start/finish line
    TelemetrySessionPtr m_rawSession;       // as the file gave it
    TelemetrySessionPtr m_recordedSession;  // as the file gave it (and what could be worked out)
    Units m_units;
    PowerEstimate::Rider m_rider;
    WeatherChoice m_weather;
    // the weather of the ride was asked for when the data was worked out, and has not come yet
    bool m_weatherWaiting{false};
};
