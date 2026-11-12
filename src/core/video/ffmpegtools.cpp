#include "ffmpegtools.h"

#include "../settings/appsettings.h"

#include <QFileInfo>
#include <QProcess>

QString FfmpegTools::ffmpegProgram()
{
    const QString configured = AppSettings::ffmpegPath();
    return configured.isEmpty() ? QStringLiteral("ffmpeg") : configured;
}

QString FfmpegTools::ffprobeFor(const QString &ffmpegProgram)
{
    const QFileInfo ffmpeg(ffmpegProgram);
    if (ffmpeg.fileName() == ffmpegProgram)
        {
            // a bare name: both come from the PATH
            return QStringLiteral("ffprobe");
        }
    const QString suffix = ffmpeg.suffix().isEmpty() ? QString() : "." + ffmpeg.suffix();
    return ffmpeg.absolutePath() + "/ffprobe" + suffix;
}

QString FfmpegTools::ffprobeProgram()
{
    return ffprobeFor(ffmpegProgram());
}

FfmpegTools::RunResult FfmpegTools::run(const QString &program, const QStringList &arguments, int timeoutMs)
{
    RunResult result;
    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000))
        {
            return result;
        }
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs))
        {
            process.kill();
            process.waitForFinished(2000);
            return result;
        }
    result.started = true;
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.output = process.readAllStandardOutput();
    result.errorOutput = process.readAllStandardError();
    return result;
}

QString FfmpegTools::versionLine(const QString &ffmpegProgram)
{
    const RunResult result = run(ffmpegProgram, {"-version"}, 5000);
    if (!result.started || result.exitCode != 0)
        {
            return QString();
        }
    return QString::fromLocal8Bit(result.output).section('\n', 0, 0).trimmed();
}
