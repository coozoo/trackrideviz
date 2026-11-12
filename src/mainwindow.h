#pragma once

#include "telemetry/telemetrysession.h"
#include "units/units.h"

#include <QMainWindow>
#include <QMediaPlayer>

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
    void openVideo(const QString &path);
    // Imports in a worker thread; the window stays usable meanwhile.
    void openTelemetry(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseVideo();
    void chooseTelemetry();
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

    QDockWidget *m_sourcesDock{nullptr};
    QDockWidget *m_libraryDock{nullptr};
    QDockWidget *m_propertiesDock{nullptr};
    QDockWidget *m_timelineDock{nullptr};

    QString m_videoPath;
    QString m_telemetryPath;
    // Import in progress; a result for any other path is stale and dropped.
    QString m_pendingTelemetryPath;
    TelemetrySessionPtr m_session;
    Units m_units;
    // The user set the offset by hand; automatic sync must not overwrite it.
    bool m_offsetIsManual{false};
};
