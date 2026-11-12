#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Finding and running the external ffmpeg / ffprobe programs.
namespace FfmpegTools
{
// From the settings, or plain "ffmpeg" to be found on the PATH.
QString ffmpegProgram();
// ffprobe lives next to ffmpeg.
QString ffprobeProgram();
QString ffprobeFor(const QString &ffmpegProgram);

struct RunResult
{
    bool started{false};   // false: not found, not executable, or it did not finish in time
    int exitCode{-1};
    QByteArray output;
    QByteArray errorOutput;
};

// Runs a short command to its end. Blocking: call from a worker thread.
RunResult run(const QString &program, const QStringList &arguments, int timeoutMs = 15000);

// First line of `ffmpeg -version`, e.g. "ffmpeg version 8.1.2 Copyright ...";
// empty when the program cannot be run.
QString versionLine(const QString &ffmpegProgram);
}
