#include "exportjob.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#endif

namespace
{
// How much may wait in the pipe before drawing pauses. Enough to keep ffmpeg
// fed, small enough that cancelling and memory stay under control.
const int kFramesAhead = 4;
const int kReportIntervalMs = 200;
const int kErrorTailBytes = 2000;
const int kStopTimeoutMs = 120000;
// time for ffmpeg to take the last pictures out of the pipe and encode them
const int kHurryDelayMs = 1500;
}

ExportJob::ExportJob(const QString &ffmpegProgram, const ExportPlan &plan, const Overlay &overlay,
                     const TelemetrySessionPtr &session, double syncOffset, QObject *parent)
    : QObject(parent)
    , m_ffmpegProgram(ffmpegProgram)
    , m_plan(plan)
    , m_overlay(overlay)
    , m_session(session)
    , m_syncOffset(syncOffset)
{
    qRegisterMetaType<ExportJob::Outcome>();
}

ExportJob::~ExportJob()
{
    if (m_done || !m_process)
        {
            return;
        }
    m_done = true; // no signals from a destructor
    for (QProcess *process : {m_process, m_trimProcess})
        {
            if (process)
                {
                    process->disconnect(this);
                    process->kill();
                    process->waitForFinished(5000);
                }
        }
    QFile::remove(m_plan.outputPath);
    QFile::remove(partPath());
}

void ExportJob::start()
{
    if (!m_plan.overlayRect.isEmpty())
        {
            m_frame = QImage(m_plan.overlayRect.size(), QImage::Format_RGBA8888_Premultiplied);
        }
    m_process = new QProcess(this);
    connect(m_process, &QProcess::started, this, &ExportJob::pump);
    connect(m_process, &QProcess::bytesWritten, this, &ExportJob::pump);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]()
    {
        m_errorOutput.append(m_process->readAllStandardError());
        // ffmpeg can be chatty for hours; only the end explains a failure
        if (m_errorOutput.size() > 4 * kErrorTailBytes)
            {
                m_errorOutput = m_errorOutput.right(kErrorTailBytes);
            }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ExportJob::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ExportJob::onProcessError);
    m_clock.start();
    m_process->start(m_ffmpegProgram, m_plan.arguments);
}

void ExportJob::stop()
{
    if (m_done || m_stopping || m_abandoned)
        {
            return;
        }
    const bool running = m_process && m_process->state() == QProcess::Running;
    if (!running || m_plan.frameCount == 0 || m_nextFrame == 0)
        {
            // not started yet, or nothing is piped and so nothing tells ffmpeg where to end
            abandon();
            return;
        }
    m_stopping = true;
    if (!m_inputClosed)
        {
            m_inputClosed = true;
            // what is already queued is still delivered, then ffmpeg sees the end
            m_process->closeWriteChannel();
        }
    QTimer::singleShot(kHurryDelayMs, this, &ExportJob::hurryUp);
    // If it does not come to an end at all, give up.
    QTimer::singleShot(kStopTimeoutMs, this, [this]()
    {
        if (!m_done && m_process->state() != QProcess::NotRunning)
            {
                abandon();
            }
    });
}

// With the picture finished, ffmpeg would go on reading the source to its
// end just to copy the remaining sound - many seconds for a long recording,
// only for that sound to be trimmed away afterwards. An interrupt makes it
// close the file right away, as pressing "q" in a terminal does. By then the
// pipe is closed and empty, so nothing is written to a process that has left.
// Windows has no such signal for a child process; there ffmpeg reads on.
void ExportJob::hurryUp()
{
#ifdef Q_OS_UNIX
    if (m_done || m_abandoned || !m_process || m_process->state() != QProcess::Running)
        {
            return;
        }
    if (m_process->bytesToWrite() > 0)
        {
            QTimer::singleShot(kHurryDelayMs, this, &ExportJob::hurryUp);
            return;
        }
    m_interrupted = true;
    ::kill(pid_t(m_process->processId()), SIGINT);
#endif
}

void ExportJob::abandon()
{
    m_abandoned = true;
    if (m_process && m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
        }
    else
        {
            finish(Cancelled, QString());
        }
}

void ExportJob::renderFrame(qint64 index)
{
    m_frame.fill(Qt::transparent);
    QPainter painter(&m_frame);
    // the picture is a window onto the full output frame
    painter.translate(-m_plan.overlayRect.topLeft());
    m_overlay.render(painter, QRectF(QPointF(0, 0), QSizeF(m_plan.outputSize)), m_session.get(),
                     m_plan.frameTime(index) + m_syncOffset);
}

void ExportJob::pump()
{
    if (m_done || m_stopping || m_abandoned || !m_process || m_process->state() != QProcess::Running)
        {
            return;
        }
    if (m_plan.frameCount == 0)
        {
            if (!m_inputClosed)
                {
                    m_inputClosed = true;
                    m_process->closeWriteChannel();
                }
            return;
        }
    const qint64 frameBytes = m_frame.sizeInBytes();
    while (m_nextFrame < m_plan.frameCount && m_process->bytesToWrite() < kFramesAhead * frameBytes)
        {
            renderFrame(m_nextFrame);
            m_process->write(reinterpret_cast<const char *>(m_frame.constBits()), frameBytes);
            ++m_nextFrame;
        }
    if (m_nextFrame >= m_plan.frameCount && !m_inputClosed)
        {
            m_inputClosed = true;
            // ffmpeg finishes the file once it has read the last picture
            m_process->closeWriteChannel();
        }
    const qint64 now = m_clock.elapsed();
    if (now - m_lastReportMs >= kReportIntervalMs || m_inputClosed)
        {
            m_lastReportMs = now;
            emit progress(m_nextFrame, m_plan.frameCount, now > 0 ? m_nextFrame * 1000.0 / now : 0.0);
        }
}

QString ExportJob::errorTail() const
{
    return QString::fromLocal8Bit(m_errorOutput.right(kErrorTailBytes)).trimmed();
}

void ExportJob::onProcessFinished()
{
    m_errorOutput.append(m_process->readAllStandardError());
    // interrupted, ffmpeg finishes the file and reports 255
    const bool clean = m_process->exitStatus() == QProcess::NormalExit
                       && (m_process->exitCode() == 0 || (m_interrupted && m_process->exitCode() == 255));
    if (m_abandoned)
        {
            finish(Cancelled, QString());
        }
    else if (clean && m_stopping && m_nextFrame < m_plan.frameCount)
        {
            if (m_plan.hasAudio)
                {
                    startTrim();
                }
            else
                {
                    finish(Stopped, QString());
                }
        }
    else if (clean)
        {
            // also when the stop came after the last picture: the file is complete
            finish(Succeeded, QString());
        }
    else if (m_stopping)
        {
            finish(Cancelled, QString());
        }
    else
        {
            const QString details = errorTail();
            finish(Failed, details.isEmpty() ? tr("ffmpeg stopped with an error and gave no explanation.")
                   : tr("ffmpeg reported:\n%1").arg(details));
        }
}

QString ExportJob::partPath() const
{
    // same folder (a rename, not a copy) and same suffix (ffmpeg picks the container by it)
    const QFileInfo output(m_plan.outputPath);
    return QString("%1/%2.part.%3").arg(output.absolutePath(), output.completeBaseName(), output.suffix());
}

// The picture ended where the user stopped, but copied sound runs on to the
// end of the source. Rewriting the file without re-encoding cuts the sound
// to the length of the picture.
void ExportJob::startTrim()
{
    QFile::remove(partPath());
    if (!QFile::rename(m_plan.outputPath, partPath()))
        {
            // playable as it is, only with sound past the end
            finish(Stopped, QString());
            return;
        }
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-i", partPath(),
                        "-t", QString::number(m_plan.lengthOf(m_nextFrame), 'f', 3),
                        "-map", "0", "-c", "copy"
                       };
    args << m_plan.muxerArguments << m_plan.outputPath;
    m_trimProcess = new QProcess(this);
    connect(m_trimProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &ExportJob::onTrimFinished);
    connect(m_trimProcess, &QProcess::errorOccurred, this, [this]()
    {
        if (m_trimProcess->error() == QProcess::FailedToStart)
            {
                onTrimFinished();
            }
    });
    m_trimProcess->start(m_ffmpegProgram, args, QIODevice::ReadOnly);
}

void ExportJob::onTrimFinished()
{
    if (m_done)
        {
            return;
        }
    const bool ok = m_trimProcess->exitStatus() == QProcess::NormalExit && m_trimProcess->exitCode() == 0
                    && m_trimProcess->error() != QProcess::FailedToStart;
    if (ok)
        {
            QFile::remove(partPath());
        }
    else
        {
            // the untrimmed file is still the export; put it back
            QFile::remove(m_plan.outputPath);
            QFile::rename(partPath(), m_plan.outputPath);
        }
    finish(Stopped, QString());
}

void ExportJob::onProcessError()
{
    // crashes and kills also arrive through finished(); only a failed start does not
    if (m_process->error() == QProcess::FailedToStart)
        {
            finish(Failed, tr("ffmpeg could not be started (%1). Install it, or set its location in the settings.")
                   .arg(m_ffmpegProgram));
        }
}

void ExportJob::finish(Outcome outcome, const QString &message)
{
    if (m_done)
        {
            return;
        }
    m_done = true;
    if (outcome == Cancelled || outcome == Failed)
        {
            // what ffmpeg leaves behind when it is killed or gives up cannot be
            // played, yet looks like a finished video in a file manager
            QFile::remove(m_plan.outputPath);
        }
    emit finished(outcome, message);
}
