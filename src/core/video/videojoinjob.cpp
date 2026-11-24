#include "videojoinjob.h"

#include "ffmpegtools.h"
#include "libav.h"
#include "libavexport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QThread>

VideoJoinJob::VideoJoinJob(ExportJob::Engine engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine == ExportJob::OwnEngine && Libav::available() ? ExportJob::OwnEngine : ExportJob::ProgramEngine)
{
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, [this]()
    {
        if (m_seconds)
            {
                emit progress(*m_seconds);
            }
    });
}

VideoJoinJob::~VideoJoinJob()
{
    cancel();
}

bool VideoJoinJob::isRunning() const
{
    return m_process || m_thread;
}

void VideoJoinJob::startCopy(const QStringList &paths, const QString &output, bool cameraData)
{
    m_output = output;
    if (m_engine == ExportJob::ProgramEngine)
        {
            m_listDir = std::make_unique<QTemporaryDir>();
            const QString list = m_listDir->filePath("join.ffconcat");
            QFile file(list);
            if (!m_listDir->isValid() || !file.open(QIODevice::WriteOnly) || file.write(VideoJoin::listFile(paths)) < 0)
                {
                    // as every failure: told once the caller is listening
                    QMetaObject::invokeMethod(this, [this]() { done(false, tr("A temporary file could not be written.")); },
                                              Qt::QueuedConnection);
                    return;
                }
            file.close();
            startProgram(VideoJoin::arguments(list, output, cameraData));
            return;
        }
    startOwn([paths, output, cameraData](const std::atomic<bool> &stop, std::atomic<double> &seconds)
    {
        QString error;
        const bool ok = Libav::copyJoin(paths, output, cameraData, [&](double at)
        {
            seconds = at;
            return !stop;
        }, &error);
        return ok ? QString() : error.isEmpty() ? QStringLiteral("stopped") : error;
    });
}

void VideoJoinJob::startEncode(const QVector<VideoJoin::Part> &parts, const QString &output, const VideoEncoder &encoder,
                               const ExportSettings &settings)
{
    m_output = output;
    if (m_engine == ExportJob::ProgramEngine)
        {
            startProgram(VideoJoin::encodeArguments(parts, output, encoder, settings));
            return;
        }
    const ExportPlan plan = VideoJoin::encodePlan(parts, output, encoder, settings);
    startOwn([plan](const std::atomic<bool> &stop, std::atomic<double> &seconds)
    {
        // nothing drawn: no gauges in the plan
        LibavExport run(plan, [](qint64) { return QImage(); });
        QString error;
        if (!run.start(&error))
            {
                return error;
            }
        LibavExport::State state = LibavExport::Working;
        while (state == LibavExport::Working && !stop)
            {
                state = run.step();
                seconds = run.secondsDone();
            }
        if (stop)
            {
                return QStringLiteral("stopped");
            }
        return state == LibavExport::Done ? QString() : run.error();
    });
}

void VideoJoinJob::startProgram(const QStringList &arguments)
{
    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this]()
    {
        const bool ok = m_process->exitStatus() == QProcess::NormalExit && m_process->exitCode() == 0;
        const QString errors = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        m_process->deleteLater();
        m_process = nullptr;
        done(ok, errors.right(600));
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]()
    {
        const double at = VideoJoin::progressSeconds(m_process->readAllStandardOutput());
        if (at >= 0.0)
            {
                emit progress(at);
            }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
    {
        if (error == QProcess::FailedToStart)
            {
                // no finished() follows a process that never ran: let it go now
                m_process->deleteLater();
                m_process = nullptr;
                done(false, tr("ffmpeg could not be started; its location can be set in the settings."));
            }
    });
    m_process->start(FfmpegTools::ffmpegProgram(), arguments);
}

void VideoJoinJob::startOwn(OwnWork work)
{
    m_stop = std::make_shared<std::atomic<bool>>(false);
    m_seconds = std::make_shared<std::atomic<double>>(0.0);
    m_error = std::make_shared<QString>();
    // the flags are kept alive by the worker too, whatever becomes of this job
    m_thread = QThread::create([work = std::move(work), stop = m_stop, seconds = m_seconds, error = m_error]()
    {
        *error = work(*stop, *seconds);
    });
    connect(m_thread, &QThread::finished, this, [this, thread = m_thread]()
    {
        // posted from the worker just before a cancel let it go: not news any more
        if (m_thread != thread)
            {
                return;
            }
        const QString error = *m_error;
        m_thread->deleteLater();
        m_thread = nullptr;
        m_poll.stop();
        // where it got to: a short join may end before the first look
        emit progress(*m_seconds);
        done(error.isEmpty(), error);
    });
    m_thread->start();
    m_poll.start();
}

void VideoJoinJob::cancel()
{
    if (m_process)
        {
            // its end is not news any more
            m_process->disconnect(this);
            m_process->kill();
            m_process->waitForFinished(3000);
            m_process->deleteLater();
            m_process = nullptr;
            QFile::remove(m_output);
        }
    if (m_thread)
        {
            m_thread->disconnect(this);
            *m_stop = true;
            // the worker writes into the file: it is removed once the worker has let it go
            m_thread->wait();
            delete m_thread;
            m_thread = nullptr;
            m_poll.stop();
            QFile::remove(m_output);
        }
}

void VideoJoinJob::done(bool ok, const QString &error)
{
    if (!ok)
        {
            QFile::remove(m_output);
        }
    emit finished(ok, ok ? QString() : error);
}
