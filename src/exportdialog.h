#pragma once

#include "overlay/overlay.h"
#include "video/exportjob.h"
#include "video/videobackground.h"
#include "video/exportprofile.h"
#include "video/mediainfo.h"
#include "video/videoencoders.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QTimeEdit;

// Choices for one export, then its progress.
//
// Opening the dialog asks ffprobe about the video and ffmpeg about the
// encoders it can really use here; both run in worker threads and the
// controls fill in when the answers arrive. The export itself is an
// ExportJob in its own thread.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    // currentPosition: where the preview stands, offered as the start of a partial export.
    // cuts: parts of the video left out. soundtrack: music and volumes.
    ExportDialog(const QString &videoPath, const Overlay &overlay, const SyncMap &sync,
                 const QVector<TimeRange> &cuts, const Soundtrack &soundtrack, double currentPosition,
                 QWidget *parent = nullptr);
    ~ExportDialog() override;
    // No video (an empty videoPath): the gauges over `background`, `length` seconds long; the
    // file is named after `namedAfter` (the data's file).
    void setBackground(const VideoBackground &background, double length, const QString &namedAfter);

public slots:
    // Closing while an export runs asks first, then stops it.
    void reject() override;

signals:
    void stopRequested();

private slots:
    void browseOutput();
    void startExport();
    void onProgress(qint64 frame, qint64 total, double framesPerSecond);
    void onFinished(ExportJob::Outcome outcome, const QString &message);
    void updateRateControls();
    void onEncoderChanged();
    void updateEstimate();
    void onAudioChanged();
    void onContainerChosen();
    void onPictureChanged();
    void showContainer();
    void onProfileChosen();
    void onChoiceChanged();
    void saveProfile();
    void deleteProfile();

private:
    void createWidgets();
    void startProbing();
    void onProbed(bool ok, const MediaInfo &info, const QString &error);
    void onEncodersDetected(const QVector<VideoEncoder> &encoders);
    void updateReadiness();
    void updateAudioControls();
    void fillProfiles(const QString &selected);
    void fillContainers();
    bool gaugesOnly() const;
    // The profile chosen; false for none ("Custom").
    bool chosenProfile(ExportProfile *profile) const;
    // Fills the controls from the chosen profile once the video and the encoders are known.
    void applyProfile();
    void showSettings(const ExportSettings &settings);
    void setRunning(bool running);
    ExportSettings currentSettings() const;
    QString suggestedOutput() const;

    QString m_videoPath;
    VideoBackground m_background;   // with no video
    QString m_namedAfter;           // what the file is named after: the video, else the data
    Overlay m_overlay;
    SyncMap m_sync;
    QVector<TimeRange> m_cuts;
    Soundtrack m_soundtrack;
    double m_currentPosition{0.0};
    QString m_ffmpeg;
    bool m_ownEngine{false};   // the program's own video engine runs the export, not ffmpeg

    bool m_probed{false};
    MediaInfo m_info;
    bool m_encodersKnown{false};
    QVector<VideoEncoder> m_encoders;
    // What the chosen profile put into the controls: a change from it is a choice of one's own.
    ExportSettings m_profileSettings;
    bool m_profileShown{false};
    bool m_showing{false};
    bool m_running{false};
    QString m_videoSuffix;      // the file type the video had before the gauges alone were chosen
    QThread *m_thread{nullptr};

    QLabel *m_sourceLabel{nullptr};
    QLabel *m_engineLabel{nullptr};
    QComboBox *m_pictureCombo{nullptr};
    QComboBox *m_profileCombo{nullptr};
    QPushButton *m_saveProfileButton{nullptr};
    QPushButton *m_deleteProfileButton{nullptr};
    QLineEdit *m_outputEdit{nullptr};
    QPushButton *m_browseButton{nullptr};
    QComboBox *m_containerCombo{nullptr};
    QComboBox *m_encoderCombo{nullptr};
    QComboBox *m_rateCombo{nullptr};
    QSpinBox *m_qualitySpin{nullptr};
    QDoubleSpinBox *m_bitrateSpin{nullptr};
    QLabel *m_rateNote{nullptr};
    QComboBox *m_speedCombo{nullptr};
    QComboBox *m_sizeCombo{nullptr};
    QComboBox *m_audioCombo{nullptr};
    QComboBox *m_audioBitrateCombo{nullptr};
    QCheckBox *m_rangeCheck{nullptr};
    QTimeEdit *m_startEdit{nullptr};
    QTimeEdit *m_endEdit{nullptr};
    QWidget *m_options{nullptr};
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusLabel{nullptr};
    QPushButton *m_exportButton{nullptr};
    QPushButton *m_closeButton{nullptr};
};
