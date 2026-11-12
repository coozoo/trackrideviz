#pragma once

#include "project/project.h"
#include "telemetry/telemetrysession.h"
#include "units/units.h"

#include <QMainWindow>
#include <QMediaPlayer>

class QAction;
class QAudioOutput;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QToolButton;
class QTreeWidget;
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
    void openVideo(const QString &path);
    // Imports in a worker thread; the window stays usable meanwhile.
    void openTelemetry(const QString &path);
    void openProject(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseVideo();
    void chooseTelemetry();
    void chooseProject();
    // Both return false when the user backed out or writing failed.
    bool saveProject();
    bool saveProjectAs();
    void exportVideo();
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
    void showAbout();

private:
    QWidget *createCentralWidget();
    void createDocks();
    void createMenus();
    QDockWidget *addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area);
    // session is null when the import failed; error then says why.
    void telemetryImported(const QString &path, const TelemetrySessionPtr &session, const QString &error);
    void loadVideo(const QString &path);
    // keepSync: the offset comes with the project and must survive the import
    void loadTelemetry(const QString &path, bool keepSync);
    void rebuildOverlay();
    void updateWindowTitle();
    void setSyncOffset(double seconds, const QString &explanation);
    void updateSourcesInfo();
    // Fills the "now" column of the channel table for the frame on screen.
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
    QTreeWidget *m_channelTree{nullptr};
    QAction *m_exportAction{nullptr};

    QDockWidget *m_sourcesDock{nullptr};
    QDockWidget *m_libraryDock{nullptr};
    QDockWidget *m_propertiesDock{nullptr};
    QDockWidget *m_timelineDock{nullptr};

    Project m_project;
    QString m_projectPath;   // empty until saved or opened
    // Import in progress; a result for any other path is stale and dropped.
    QString m_pendingTelemetryPath;
    bool m_pendingKeepsSync{false};
    TelemetrySessionPtr m_session;
    Units m_units;
};
