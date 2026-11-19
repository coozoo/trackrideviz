#pragma once

#include "exportjob.h"
#include "videojoin.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

class QProcess;
class QThread;

// Runs one join (see VideoJoin) on either engine: the ffmpeg program, or FFmpeg's libraries
// in a thread of its own (only when built with them). Lives in the GUI thread; nothing here
// blocks it.
class VideoJoinJob : public QObject
{
    Q_OBJECT

public:
    explicit VideoJoinJob(ExportJob::Engine engine, QObject *parent = nullptr);
    // A job still running is stopped and its unfinished file removed.
    ~VideoJoinJob() override;

    // What VideoJoin::listFile names, copied into `output`.
    void startCopy(const QString &listFile, const QString &output, bool cameraData);
    // VideoJoin::encodePlan run.
    void startEncode(const QVector<VideoJoin::Part> &parts, const QString &output, const VideoEncoder &encoder,
                     const ExportSettings &settings);
    // Stops it, removes the unfinished file; no finished() follows.
    void cancel();
    bool isRunning() const;

signals:
    // seconds of the joined video written so far
    void progress(double seconds);
    // error: why it failed, in the user's language; empty when it succeeded
    void finished(bool ok, const QString &error);

private:
    void startProgram(const QStringList &arguments);
    // the work done in the worker thread: an error, "stopped", or empty when it succeeded
    using OwnWork = std::function<QString(const std::atomic<bool> &stop, std::atomic<double> &seconds)>;
    void startOwn(OwnWork work);
    void done(bool ok, const QString &error);

    ExportJob::Engine m_engine;
    QString m_output;
    QProcess *m_process{nullptr};
    QThread *m_thread{nullptr};
    QTimer m_poll;
    // shared with the worker thread
    std::shared_ptr<std::atomic<bool>> m_stop;
    std::shared_ptr<std::atomic<double>> m_seconds;
    std::shared_ptr<QString> m_error;   // written by the worker, read once it has finished
};
