#include "exportjob.h"

#include "../settings/appsettings.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QThread>
#include <QTimer>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <fcntl.h>
#endif

namespace
{
// How much may wait in the pipe before drawing pauses. Enough to keep ffmpeg
// fed, small enough that canceling and memory stay under control.
const int kFramesAhead = 4;
const int kReportIntervalMs = 200;
const int kErrorTailBytes = 2000;
const int kStopTimeoutMs = 120000;
// time for ffmpeg to take the last pictures out of the pipe and encode them
const int kHurryDelayMs = 1500;
}

ExportJob::ExportJob(const QString &ffmpegProgram, const ExportPlan &plan, const Overlay &overlay,
                     const SyncMap &sync, QObject *parent, Engine engine)
    : QObject(parent)
    , m_engine(engine)
    , m_ffmpegProgram(ffmpegProgram)
    , m_plan(plan)
    , m_overlay(overlay)
    , m_sync(sync)
{
    qRegisterMetaType<ExportJob::Outcome>();
}

ExportJob::~ExportJob()
{
    // what is drawn ahead is not needed any more
    m_drawPool.clear();
    m_drawPool.waitForDone();
    if (m_own)
        {
            if (!m_done)
                {
                    // closed first, then the unfinished file goes
                    m_own.reset();
                    QFile::remove(m_plan.outputPath);
                }
            return;
        }
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

int ExportJob::drawersFor(bool allCores)
{
    const int cores = std::max(1, QThread::idealThreadCount());
    return allCores ? cores : std::max(1, cores / 2);
}

void ExportJob::start()
{
    if (m_drawers <= 0)
        {
            m_drawers = drawersFor(AppSettings::exportAllCores());
        }
    m_drawPool.setMaxThreadCount(m_drawers);
    m_frameBytes = qint64(m_plan.stackSize.width()) * m_plan.stackSize.height() * 4;
    if (m_engine == OwnEngine)
        {
            m_own = std::make_unique<LibavExport>(m_plan, [this](qint64 index) { return drawn(index); });
            m_clock.start();
            QString error;
            if (!m_own->start(&error))
                {
                    m_own.reset();
                    // reported once start() has returned, as a failed ffmpeg is
                    QTimer::singleShot(0, this, [this, error]()
                    {
                        finish(Failed, tr("The video engine could not start: %1").arg(error));
                    });
                    return;
                }
            QTimer::singleShot(0, this, &ExportJob::stepOwn);
            return;
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
    // Ctrl+C in a terminal reaches the whole group of processes in front: ffmpeg
    // would close the file and leave with 255 before the job knows it is to stop,
    // and what it wrote would be thrown away as broken. In a group of its own it
    // hears of a stop only from the job.
#ifdef Q_OS_UNIX
    m_process->setChildProcessModifier([]()
    {
        ::setpgid(0, 0);
#ifdef Q_OS_LINUX
        // A pipe holds 64 KB; a picture of gauges is megabytes, so every frame
        // would take dozens of write / wake-up rounds. The child enlarges its
        // end before ffmpeg starts (a pipe has one buffer for both ends); the
        // system may cap the size, and failing changes nothing but speed.
        ::fcntl(0, F_SETPIPE_SZ, 1024 * 1024);
#endif
    });
#endif
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments)
    {
        arguments->flags |= CREATE_NEW_PROCESS_GROUP;
    });
#endif
    m_clock.start();
    m_process->start(m_ffmpegProgram, m_plan.arguments);
}

void ExportJob::stop()
{
    if (m_done || m_stopping || m_abandoned)
        {
            return;
        }
    if (m_engine == OwnEngine)
        {
            if (!m_own || m_plan.frameCount == 0 || m_own->gaugesSent() == 0)
                {
                    // nothing tells the engine where to end, or nothing is worth keeping
                    m_abandoned = true;
                    m_own.reset();
                    finish(Canceled, QString());
                    return;
                }
            // the file ends after the pictures sent so far, its sound with it
            m_stopping = true;
            m_own->endGauges();
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

void ExportJob::cancel()
{
    if (!m_done && !m_abandoned)
        {
            abandon();
        }
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
            finish(Canceled, QString());
        }
}

void ExportJob::stepOwn()
{
    if (m_done || !m_own)
        {
            return;
        }
    const LibavExport::State state = m_own->step();
    m_nextFrame = m_own->gaugesSent();
    const qint64 now = m_clock.elapsed();
    if (now - m_lastReportMs >= kReportIntervalMs || state != LibavExport::Working)
        {
            m_lastReportMs = now;
            emit progress(m_nextFrame, m_plan.frameCount, now > 0 ? m_nextFrame * 1000.0 / now : 0.0);
        }
    switch (state)
        {
        case LibavExport::Working:
            // back to the event loop between slices, so a stop gets through
            QTimer::singleShot(0, this, &ExportJob::stepOwn);
            return;
        case LibavExport::Done:
            m_own.reset();
            finish(m_stopping && m_nextFrame < m_plan.frameCount ? Stopped : Succeeded, QString());
            return;
        case LibavExport::Failed:
            {
                const QString error = m_own->error();
                m_own.reset();
                finish(Failed, tr("The video engine reported:\n%1").arg(error));
                return;
            }
        }
}

QImage ExportJob::drawn(qint64 index)
{
    // the frames up to as many ahead as there are drawers, each started once; those before
    // `index` are not wanted any more (a stop, a skip)
    m_ahead.erase(m_ahead.begin(), m_ahead.lower_bound(index));
    const qint64 last = std::min(index + m_drawers, std::max(m_plan.frameCount, index + 1));
    for (qint64 next = index; next < last; ++next)
        {
            if (m_ahead.count(next) == 0)
                {
                    auto task = std::make_shared<std::packaged_task<QImage()>>([this, next]() { return draw(next); });
                    m_ahead.emplace(next, task->get_future());
                    m_drawPool.start([task]() { (*task)(); });
                }
        }
    const auto at = m_ahead.find(index);
    const QImage picture = at->second.get();
    m_ahead.erase(at);
    return picture;
}

QImage ExportJob::draw(qint64 index) const
{
    if (m_plan.regions.isEmpty())
        {
            return QImage();
        }
    // Qt's own format: drawing into anything else converts at every step
    QImage picture(m_plan.stackSize, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    const QRectF frame(QPointF(0, 0), QSizeF(m_plan.outputSize));
    const double time = m_sync.telemetryTime(m_plan.frameTime(index));
    for (int i = 0; i < m_plan.regions.size(); ++i)
        {
            // each region is a window onto the full output frame, at its place in the stack
            const QRect &region = m_plan.regions.at(i);
            painter.save();
            painter.translate(-region.x(), m_plan.stackTops.at(i) - region.y());
            painter.setClipRect(region);
            m_overlay.render(painter, frame, time, region);
            painter.restore();
        }
    painter.end();
    // over nothing the colors go with straight alpha (see ExportPlan)
    return m_plan.transparent ? picture.convertToFormat(QImage::Format_ARGB32) : picture;
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
    const qint64 frameBytes = m_frameBytes;
    while (m_nextFrame < m_plan.frameCount && m_process->bytesToWrite() < kFramesAhead * frameBytes)
        {
            const QImage picture = drawn(m_nextFrame);
            m_process->write(reinterpret_cast<const char *>(picture.constBits()), frameBytes);
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
            finish(Canceled, QString());
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
            finish(Canceled, QString());
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
    if (outcome == Canceled || outcome == Failed)
        {
            // what ffmpeg leaves behind when it is killed or gives up cannot be
            // played, yet looks like a finished video in a file manager
            QFile::remove(m_plan.outputPath);
        }
    emit finished(outcome, message);
}
