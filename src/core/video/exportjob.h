#pragma once

#include "../overlay/overlay.h"
#include "../telemetry/telemetrysession.h"
#include "exportplan.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>

class QProcess;

// Runs one export: starts ffmpeg, draws the gauges for every frame and
// writes them into its standard input.
//
// A worker object: move it to its own thread and call start() through a
// signal. Nothing here blocks for long, so stop() sent from the GUI thread
// is handled between frames. The session and the overlay are only read.
//
// Stopping does not kill ffmpeg. The pipe is closed, ffmpeg takes that as
// the end of the film and finishes the file properly, so what was exported
// so far can be played. ffmpeg is killed only when there is nothing worth
// keeping, or when it does not come to an end.
class ExportJob : public QObject
{
    Q_OBJECT

public:
    enum Outcome
    {
        Succeeded,
        Stopped,     // by the user; the part done so far is a playable file
        Cancelled,   // by the user; nothing was kept
        Failed
    };
    Q_ENUM(Outcome)

    ExportJob(const QString &ffmpegProgram, const ExportPlan &plan, const Overlay &overlay,
              const TelemetrySessionPtr &session, double syncOffset, QObject *parent = nullptr);
    // Destroying a job that is still running stops ffmpeg and removes the unfinished file.
    ~ExportJob() override;

public slots:
    void start();
    // Ends the export early, keeping what is done. Harmless when called twice or too late.
    void stop();

signals:
    // total is 0 when nothing is piped and the length is not known to us
    void progress(qint64 frame, qint64 total, double framesPerSecond);
    // message: why it failed, in the user's language; empty otherwise
    void finished(ExportJob::Outcome outcome, const QString &message);

private slots:
    void pump();
    void onProcessFinished();
    void onTrimFinished();
    void onProcessError();

private:
    void renderFrame(qint64 index);
    void abandon();
    void hurryUp();
    void startTrim();
    QString partPath() const;
    void finish(Outcome outcome, const QString &message);
    QString errorTail() const;

    QString m_ffmpegProgram;
    ExportPlan m_plan;
    Overlay m_overlay;
    TelemetrySessionPtr m_session;
    double m_syncOffset{0.0};

    QProcess *m_process{nullptr};
    QProcess *m_trimProcess{nullptr};
    QImage m_frame;
    qint64 m_nextFrame{0};
    bool m_inputClosed{false};
    bool m_stopping{false};     // pipe closed on request, ffmpeg is finishing the file
    bool m_interrupted{false};  // asked ffmpeg to wrap up at once (see hurryUp)
    bool m_abandoned{false};    // ffmpeg killed, nothing will be kept
    bool m_done{false};
    QByteArray m_errorOutput;
    QElapsedTimer m_clock;
    qint64 m_lastReportMs{0};
};
