#pragma once

#include "overlay/overlay.h"
#include "video/exportjob.h"
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
    // currentPosition: where the preview stands, offered as the start of a partial export
    ExportDialog(const QString &videoPath, const Overlay &overlay,
                 double syncOffset, double currentPosition, QWidget *parent = nullptr);
    ~ExportDialog() override;

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

private:
    void createWidgets();
    void startProbing();
    void onProbed(bool ok, const MediaInfo &info, const QString &error);
    void onEncodersDetected(const QVector<VideoEncoder> &encoders);
    void updateReadiness();
    void setRunning(bool running);
    ExportSettings currentSettings() const;
    QString suggestedOutput() const;

    QString m_videoPath;
    Overlay m_overlay;
    double m_syncOffset{0.0};
    double m_currentPosition{0.0};
    QString m_ffmpeg;

    bool m_probed{false};
    MediaInfo m_info;
    bool m_encodersKnown{false};
    bool m_running{false};
    QThread *m_thread{nullptr};

    QLabel *m_sourceLabel{nullptr};
    QLineEdit *m_outputEdit{nullptr};
    QPushButton *m_browseButton{nullptr};
    QComboBox *m_encoderCombo{nullptr};
    QComboBox *m_rateCombo{nullptr};
    QSpinBox *m_qualitySpin{nullptr};
    QDoubleSpinBox *m_bitrateSpin{nullptr};
    QLabel *m_rateNote{nullptr};
    QComboBox *m_speedCombo{nullptr};
    QComboBox *m_sizeCombo{nullptr};
    QComboBox *m_audioCombo{nullptr};
    QCheckBox *m_rangeCheck{nullptr};
    QTimeEdit *m_startEdit{nullptr};
    QTimeEdit *m_endEdit{nullptr};
    QWidget *m_options{nullptr};
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusLabel{nullptr};
    QPushButton *m_exportButton{nullptr};
    QPushButton *m_closeButton{nullptr};
};
