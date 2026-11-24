#include "commandline.h"

#include "laps/lapengine.h"
#include "maps/tilestore.h"
#include "overlay/gaugetemplate.h"
#include "overlay/overlay.h"
#include "overlay/templatelibrary.h"
#include "project/project.h"
#include "project/projectdata.h"
#include "settings/appsettings.h"
#include "sync/videosync.h"
#include "telemetry/datasources.h"
#include "telemetry/telemetryimporter.h"
#include "video/exportjob.h"
#include "video/exportplan.h"
#include "video/lapexport.h"
#include "video/exportprofile.h"
#include "video/ffmpegtools.h"
#include "video/libav.h"
#include "video/mediainfo.h"
#include "video/videoencoders.h"
#include "weather/weather.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <functional>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
const char *const kCommands[] = {"--render", "--list-encoders", "--list-profiles", "--list-templates", "--help", "-h",
                                 "--help-all", "--version", "-v"};
// how long terrain heights, the weather and map tiles are waited for before the export goes on without them
const int kWaitForDownloadsMs = 60000;

std::atomic<bool> interrupted{false};

void onInterrupt(int)
{
    interrupted = true;
}

void say(const QString &text)
{
    std::fprintf(stdout, "%s\n", qPrintable(text));
    std::fflush(stdout);
}

int fail(const QString &text, int code = 1)
{
    std::fprintf(stderr, "trackrideviz: %s\n", qPrintable(text));
    return code;
}

// Waits while `still` says so, the events (downloads) going on, at most `ms`.
void waitWhile(const std::function<bool()> &still, int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (still() && clock.elapsed() < ms && !interrupted)
        {
            QEventLoop pause;
            QTimer::singleShot(250, &pause, &QEventLoop::quit);
            pause.exec();
        }
}

QString word(const char *const words[], int index)
{
    return QString::fromLatin1(words[index]);
}

template <int N>
int indexOfWord(const char *const (&words)[N], const QString &value)
{
    for (int i = 0; i < N; ++i)
        {
            if (value.compare(QLatin1String(words[i]), Qt::CaseInsensitive) == 0)
                {
                    return i;
                }
        }
    return -1;
}

QVector<ExportProfile> allProfiles()
{
    return ExportProfiles::builtIn() + AppSettings::exportProfiles();
}

const ExportProfile *profileNamed(const QVector<ExportProfile> &profiles, const QString &name)
{
    for (const ExportProfile &profile : profiles)
        {
            if (profile.id.compare(name, Qt::CaseInsensitive) == 0 || profile.name.compare(name, Qt::CaseInsensitive) == 0)
                {
                    return &profile;
                }
        }
    return nullptr;
}

QString codecName(VideoEncoder::Codec codec)
{
    switch (codec)
        {
        case VideoEncoder::H264:
            return QStringLiteral("H.264");
        case VideoEncoder::Hevc:
            return QStringLiteral("HEVC");
        case VideoEncoder::Av1:
            return QStringLiteral("AV1");
        }
    return QString();
}

int listProfiles()
{
    const QString current = AppSettings::exportProfile();
    say(QCoreApplication::translate("CommandLine", "Export profiles (--profile takes the name in the first column, or the whole name):"));
    for (const ExportProfile &profile : allProfiles())
        {
            QString what;
            if (profile.builtIn)
                {
                    QStringList codecs;
                    for (const VideoEncoder::Codec codec : profile.codecs)
                        {
                            codecs << codecName(codec);
                        }
                    what = QCoreApplication::translate("CommandLine", "%1, the best encoder there is for it").arg(codecs.join(QCoreApplication::translate("CommandLine", " or ")));
                }
            else
                {
                    what = QCoreApplication::translate("CommandLine", "encoder %1").arg(profile.encoder);
                }
            say(QString("  %1%2  %3 (%4), .%5")
                .arg(profile.id == current ? "*" : " ", profile.id.leftJustified(14), profile.name, what, profile.suffix));
        }
    say(QCoreApplication::translate("CommandLine", "* the one the export dialog used last, taken when --profile is not given"));
    return 0;
}

int listTemplates()
{
    say(QCoreApplication::translate("CommandLine", "Templates (--template takes the name):"));
    QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
    std::sort(entries.begin(), entries.end(), [](const TemplateLibrary::Entry &a, const TemplateLibrary::Entry &b)
    {
        return a.category == b.category ? a.name.compare(b.name, Qt::CaseInsensitive) < 0 : a.category < b.category;
    });
    for (const TemplateLibrary::Entry &entry : std::as_const(entries))
        {
            say(QString("  %1  %2%3").arg((entry.category.isEmpty() ? QStringLiteral("-") : entry.category).leftJustified(12), entry.name,
                                          entry.builtin ? QString() : QCoreApplication::translate("CommandLine", " (yours)")));
        }
    return 0;
}

// The engine and encoders of an export: the one asked for, or the other when it cannot export here.
VideoEncoders::ForEngine findEncoders(bool preferOwn)
{
    const VideoEncoders::ForEngine found = VideoEncoders::detectFor(preferOwn, FfmpegTools::ffmpegProgram());
    if (!found.encoders.isEmpty())
        {
            say(found.own ? QCoreApplication::translate("CommandLine", "Engine: built in, %1").arg(Libav::version())
                : QCoreApplication::translate("CommandLine", "Engine: the ffmpeg program, %1").arg(FfmpegTools::ffmpegProgram()));
        }
    return found;
}

int listEncoders(bool preferOwn)
{
    say(QCoreApplication::translate("CommandLine", "Trying the encoders (a short test encode each)..."));
    const QVector<VideoEncoder> found = findEncoders(preferOwn).encoders;
    if (found.isEmpty())
        {
            return fail(QCoreApplication::translate("CommandLine", "ffmpeg could not be run, or has none of the encoders this program knows."));
        }
    say(QCoreApplication::translate("CommandLine", "Encoders that work on this computer (--encoder takes the name in the first column):"));
    for (const VideoEncoder &encoder : found)
        {
            say(QString("  %1  %2  %3").arg(encoder.id.leftJustified(18), codecName(encoder.codec).leftJustified(6), encoder.label));
        }
    return 0;
}

QString describe(const ExportSettings &settings)
{
    const QString rate = settings.rateControl == ExportSettings::Quality ? QCoreApplication::translate("CommandLine", "quality %1").arg(settings.quality)
                         : QCoreApplication::translate("CommandLine", "%1 kbit/s").arg(settings.bitrateKbps);
    return QCoreApplication::translate("CommandLine", "%1, %2, %3, sound: %4").arg(settings.encoder, rate,
                                            settings.height > 0 ? QCoreApplication::translate("CommandLine", "%1 pixels high").arg(settings.height) : QCoreApplication::translate("CommandLine", "size of the video"),
                                            word(ExportSettings::AudioWords, settings.audio))
           + (settings.isSpedUp() ? QCoreApplication::translate("CommandLine", ", %1 times faster, without the camera's sound")
              .arg(settings.speedUp) : QString());
}

QString hms(double seconds)
{
    const qint64 whole = qint64(std::max(0.0, seconds));
    return QString("%1:%2:%3").arg(whole / 3600).arg((whole / 60) % 60, 2, 10, QChar('0')).arg(whole % 60, 2, 10, QChar('0'));
}

// "1920x1080", "1080x1920@60", "1920x1080@30,#202020", "1920x1080,transparent"
bool parseBackground(const QString &text, VideoBackground *background)
{
    static const QRegularExpression form(QStringLiteral("^(\\d+)x(\\d+)(?:@(\\d+))?(?:,(.+))?$"));
    const QRegularExpressionMatch match = form.match(text.trimmed());
    if (!match.hasMatch())
        {
            return false;
        }
    VideoBackground made;
    made.size = QSize(match.captured(1).toInt(), match.captured(2).toInt());
    made.fps = match.captured(3).isEmpty() ? 30 : match.captured(3).toInt();
    const QString color = match.captured(4);
    if (color.compare("transparent", Qt::CaseInsensitive) == 0)
        {
            made.color = QColor(0, 0, 0, 0);
        }
    else if (!color.isEmpty())
        {
            made.color = QColor(color);
        }
    if (!made.isOn() || made.size.width() > 8192 || made.size.height() > 8192 || made.fps > 240 || !made.color.isValid())
        {
            return false;
        }
    *background = made;
    return true;
}

int render(const QCommandLineParser &parser, bool preferOwn)
{
    // what is drawn: a project, or a video, its data and a template
    Project project;
    QString projectPath = parser.value("project");
    if (projectPath.isEmpty() && !parser.positionalArguments().isEmpty())
        {
            projectPath = parser.positionalArguments().first();
        }
    const bool adHoc = projectPath.isEmpty();
    if (!adHoc)
        {
            QString error;
            if (!ProjectFile::load(projectPath, &project, &error))
                {
                    return fail(error);
                }
        }
    else
        {
            project.layoutAspect = 0.0;
        }
    if (parser.isSet("video"))
        {
            project.videoPath = QFileInfo(parser.value("video")).absoluteFilePath();
        }
    if (parser.isSet("data"))
        {
            project.telemetryPath = QFileInfo(parser.value("data")).absoluteFilePath();
        }
    if (parser.isSet("background"))
        {
            if (parser.isSet("video"))
                {
                    return fail(QCoreApplication::translate("CommandLine", "--background is in place of a video: give one of them."), 2);
                }
            if (!parseBackground(parser.value("background"), &project.background))
                {
                    return fail(QCoreApplication::translate("CommandLine", "--background wants a size, then a frame rate and a color "
                                                            "if wanted: 1920x1080, 1080x1920@60, 1920x1080@30,#202020, "
                                                            "1920x1080,transparent."), 2);
                }
            project.videoPath.clear();
        }
    // no video: the gauges over the background, as long as the data
    const bool noVideo = project.videoPath.isEmpty();
    if (noVideo && !project.background.isOn())
        {
            return fail(QCoreApplication::translate("CommandLine", "No video: give a project, --video or --background."), 2);
        }
    if (noVideo && project.telemetryPath.isEmpty())
        {
            return fail(QCoreApplication::translate("CommandLine", "Without a video the data is needed: it is as long as the data."), 2);
        }
    if (!noVideo && !QFileInfo::exists(project.videoPath))
        {
            return fail(QCoreApplication::translate("CommandLine", "The video %1 is not there.").arg(project.videoPath));
        }

    const QString ffmpeg = FfmpegTools::ffmpegProgram();
    // the background's length is the data's, once it is read
    MediaInfo info = project.background.mediaInfo(0.0);
    QString probeError;
    if (!noVideo && !MediaProbe::probe(FfmpegTools::ffprobeProgram(), project.videoPath, &info, &probeError, preferOwn))
        {
            return fail(probeError);
        }
    const double aspect = info.size.height() > 0 ? double(info.size.width()) / info.size.height() : 16.0 / 9.0;

    // a template: in place of the project's gauges, placed for this video's shape
    if (parser.isSet("template"))
        {
            const QString wanted = parser.value("template");
            QString id;
            for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
                {
                    if (entry.name.compare(wanted, Qt::CaseInsensitive) == 0 || entry.id == wanted)
                        {
                            id = entry.id;
                        }
                }
            if (id.isEmpty())
                {
                    return fail(QCoreApplication::translate("CommandLine", "There is no template \"%1\"; --list-templates names them.").arg(wanted), 2);
                }
            const GaugeTemplate chosen = TemplateLibrary::get(id);
            project.overlay = chosen.overlayItems(aspect);
            project.theme = chosen.theme;
            project.layoutAspect = aspect;
        }
    else if (!adHoc && project.layoutAspect > 0.0 && std::abs(project.layoutAspect - aspect) > 0.01)
        {
            // placed on a video of another shape: again from their nearest edges, as the window does
            const Overlay placed(project.overlay, nullptr, AppSettings::units(), project.theme);
            project.overlay = GaugeTemplate::fromOverlay(placed, project.theme, project.layoutAspect).overlayItems(aspect);
        }

    // how it is encoded: a profile, an encoder of this computer, then what is given by hand
    const QVector<ExportProfile> profiles = allProfiles();
    QString profileName = parser.value("profile");
    if (profileName.isEmpty())
        {
            profileName = profileNamed(profiles, AppSettings::exportProfile()) ? AppSettings::exportProfile() : QString("youtube");
        }
    const ExportProfile *profile = profileNamed(profiles, profileName);
    if (!profile)
        {
            return fail(QCoreApplication::translate("CommandLine", "There is no export profile \"%1\"; --list-profiles names them.").arg(profileName), 2);
        }

    // where it goes: given, or beside the video; known before anything slow is done
    const bool gauges = parser.isSet("gauges-only") || (noVideo && project.background.isTransparent());
    QString suffix = gauges ? QStringLiteral("mov") : profile->suffix;
    const int audioAsked = parser.isSet("audio") ? indexOfWord(ExportSettings::AudioWords, parser.value("audio")) : -1;
    if (!gauges && ExportSettings::needsMatroska(audioAsked >= 0 ? ExportSettings::Audio(audioAsked) : profile->audio))
        {
            suffix = QStringLiteral("mkv");
        }
    // the file is named after the video, else the data
    const QFileInfo video(noVideo ? project.telemetryPath : project.videoPath);
    const QString outputPath = parser.isSet("output") ? QFileInfo(parser.value("output")).absoluteFilePath()
                               : video.absolutePath() + "/" + video.completeBaseName() + (gauges ? "_gauges." : "_overlay.") + suffix;
    const QFileInfo output(outputPath);
    if (!output.absoluteDir().exists())
        {
            return fail(QCoreApplication::translate("CommandLine", "The folder %1 is not there.").arg(output.absolutePath()));
        }
    // with --laps each lap's own file is looked at
    if (output.exists() && !parser.isSet("overwrite") && !parser.isSet("laps"))
        {
            return fail(QCoreApplication::translate("CommandLine", "%1 is there already; --overwrite replaces it.").arg(output.filePath()), 2);
        }

    // the data, worked out as the window does; heights of the terrain and the weather waited for
    AppSettings::applyMapSources();
    WeatherStore::instance()->setDownloads(true);
    TelemetrySessionPtr session;
    if (!project.telemetryPath.isEmpty())
        {
            say(QCoreApplication::translate("CommandLine", "Reading %1...").arg(QFileInfo(project.telemetryPath).fileName()));
            QString error;
            const TelemetrySessionPtr main = TelemetryImporter::loadRecorded(project.telemetryPath, &error);
            if (!main)
                {
                    return fail(error.isEmpty() ? QCoreApplication::translate("CommandLine", "%1 could not be read.").arg(project.telemetryPath) : error);
                }
            const TelemetrySessionPtr raw = DataSources::loadAndMerge(main, &project.moreData);
            bool waiting = false;
            bool weatherWaiting = false;
            const auto workOut = [&]()
            {
                return ProjectData::workedOut(raw, project.dataEdits, project.elevation, AppSettings::powerRider(),
                                              project.surfaces, AppSettings::weather(), project.wind,
                                              AppSettings::drivetrain(), &waiting, &weatherWaiting);
            };
            TelemetrySessionPtr worked = workOut();
            if (waiting || weatherWaiting)
                {
                    say(QCoreApplication::translate("CommandLine", "Waiting for the heights of the terrain and the weather of the ride..."));
                    waitWhile([&]()
                    {
                        worked = workOut();
                        return waiting || weatherWaiting;
                    }, kWaitForDownloadsMs);
                }
            session = LapEngine::apply(worked, project.startFinish, project.sectors, project.referenceLap);
        }
    else
        {
            say(QCoreApplication::translate("CommandLine", "No data: the gauges show none."));
        }
    if (noVideo)
        {
            info = project.background.mediaInfo(session ? session->duration() : 0.0);
            if (info.duration <= 0.0)
                {
                    return fail(QCoreApplication::translate("CommandLine", "The data has no length to make a video of."));
                }
            // the picture runs on the data's own time
            project.syncOffset = 0.0;
            project.videoShifts.clear();
        }

    if (parser.isSet("timelapse"))
        {
            bool ok = false;
            project.syncRate = parser.value("timelapse").toDouble(&ok);
            if (!ok || project.syncRate <= 0.0)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--timelapse wants the seconds of data in a second of video, as 10 or 30."), 2);
                }
        }

    // in step: as given; a project's as saved; otherwise by the clocks, as the window does
    if (parser.isSet("offset"))
        {
            bool ok = false;
            project.syncOffset = parser.value("offset").toDouble(&ok);
            if (!ok)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--offset wants seconds, as 12.5 or -3600."), 2);
                }
        }
    else if (adHoc && session && !noVideo)
        {
            if (QFileInfo(project.videoPath) == QFileInfo(project.telemetryPath))
                {
                    project.syncOffset = 0.0;
                }
            else
                {
                    const QDateTime wallClock = VideoSync::wallClockFromFileName(QFileInfo(project.videoPath).fileName());
                    const int systemUtcOffset = wallClock.isValid() ? QDateTime(wallClock.date(), wallClock.time()).offsetFromUtc() : 0;
                    // a time-lapse covers its length times its rate of the data
                    const VideoSync::Result result = VideoSync::match(*session, wallClock, info.duration * project.sync().rate, systemUtcOffset);
                    if (!result.ok)
                        {
                            return fail(QCoreApplication::translate("CommandLine", "The video and the data could not be put in step: %1 Give --offset.")
                                        .arg(result.explanation));
                        }
                    project.syncOffset = result.offsetSeconds;
                    say(QCoreApplication::translate("CommandLine", "In step: %1").arg(result.explanation));
                }
        }

    const Units units = AppSettings::units();
    Overlay overlay(project.overlay, session, units, project.theme);
    // street maps under the gauges: their tiles are asked for as the gauges are made
    if (TileStore::pendingOfAllMaps() > 0)
        {
            say(QCoreApplication::translate("CommandLine", "Waiting for the street maps..."));
            waitWhile([]() { return TileStore::pendingOfAllMaps() > 0; }, kWaitForDownloadsMs);
            overlay = Overlay(project.overlay, session, units, project.theme);
        }

    ExportSettings current = AppSettings::exportDefaults();
    QVector<VideoEncoder> available;
    // the engine: the one asked for when it can export here, else the other
    bool own = preferOwn && Libav::available();
    if (parser.isSet("encoder"))
        {
            current.encoder = parser.value("encoder");
            if (!own && Libav::available() && FfmpegTools::versionLine(ffmpeg).isEmpty())
                {
                    own = true;
                }
        }
    else
        {
            say(QCoreApplication::translate("CommandLine", "Finding the encoders this computer can use..."));
            const VideoEncoders::ForEngine found = findEncoders(preferOwn);
            own = found.own;
            available = found.encoders;
            if (available.isEmpty())
                {
                    return fail(QCoreApplication::translate("CommandLine", "ffmpeg could not be run, or has none of the encoders this program knows: %1").arg(ffmpeg));
                }
            if (parser.isSet("hardware") || parser.isSet("software"))
                {
                    const bool hardware = parser.isSet("hardware");
                    const auto found = std::find_if(available.cbegin(), available.cend(), [hardware](const VideoEncoder &encoder)
                    {
                        return encoder.hardware == hardware;
                    });
                    if (found == available.cend())
                        {
                            return fail(hardware ? QCoreApplication::translate("CommandLine", "No encoder of a graphics card works on this computer.")
                                        : QCoreApplication::translate("CommandLine", "No encoder for the processor works here."));
                        }
                    // the kind of encoder the profile picks among those for its codecs
                    current.encoder = found->id;
                }
        }
    ExportSettings settings = ExportProfiles::apply(*profile, current, info, available);
    if (parser.isSet("encoder"))
        {
            settings.encoder = parser.value("encoder");
        }
    bool ok = true;
    const auto number = [&](const char *option, int low, int high, int *into)
    {
        if (parser.isSet(option))
            {
                bool read = false;
                const int value = parser.value(option).toInt(&read);
                if (!read || value < low || value > high)
                    {
                        ok = false;
                        fail(QCoreApplication::translate("CommandLine", "--%1 wants a whole number from %2 to %3.").arg(option).arg(low).arg(high), 2);
                        return;
                    }
                *into = value;
            }
    };
    number("bitrate", 100, 1000000, &settings.bitrateKbps);
    number("quality", 0, 63, &settings.quality);
    number("height", 0, 8640, &settings.height);
    number("audio-bitrate", 32, 1024, &settings.audioKbps);
    if (!ok)
        {
            return 2;
        }
    if (parser.isSet("bitrate"))
        {
            settings.rateControl = ExportSettings::Bitrate;
        }
    else if (parser.isSet("quality"))
        {
            settings.rateControl = ExportSettings::Quality;
        }
    if (parser.isSet("speed"))
        {
            const int speed = indexOfWord(ExportSettings::SpeedWords, parser.value("speed"));
            if (speed < 0)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--speed is fast, balanced or small."), 2);
                }
            settings.speed = ExportSettings::Speed(speed);
        }
    if (parser.isSet("audio"))
        {
            const int audio = indexOfWord(ExportSettings::AudioWords, parser.value("audio"));
            if (audio < 0)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--audio is copy, aac, none, opus or vorbis."), 2);
                }
            settings.audio = ExportSettings::Audio(audio);
        }
    if (gauges)
        {
            settings.picture = ExportSettings::GaugesOnly;
            settings.audio = ExportSettings::NoAudio;
        }
    settings.background = project.background.color;
    if (parser.isSet("faster"))
        {
            bool ok = false;
            settings.speedUp = parser.value("faster").toDouble(&ok);
            if (!ok || settings.speedUp < 1.0)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--faster wants how many times faster, 1 or more, as 4."), 2);
                }
        }
    if (parser.isSet("from") || parser.isSet("to"))
        {
            bool fromOk = true;
            bool toOk = true;
            const double from = parser.isSet("from") ? parser.value("from").toDouble(&fromOk) : 0.0;
            const double to = parser.isSet("to") ? parser.value("to").toDouble(&toOk) : info.duration;
            if (!fromOk || !toOk || to <= from)
                {
                    return fail(QCoreApplication::translate("CommandLine", "--from and --to want seconds into the video, the end after the start."), 2);
                }
            settings.start = from;
            settings.duration = to - from;
        }

    settings.outputPath = outputPath;
    Soundtrack soundtrack = project.soundtrack;
    soundtrack.tracks.erase(std::remove_if(soundtrack.tracks.begin(), soundtrack.tracks.end(), [](const MusicTrack &track)
    {
        return !QFileInfo::exists(track.path);
    }), soundtrack.tracks.end());
    QStringList inputs{project.videoPath};
    for (const MusicTrack &track : std::as_const(soundtrack.tracks))
        {
            inputs << track.path;
        }
    for (const QString &input : std::as_const(inputs))
        {
            if (output == QFileInfo(input))
                {
                    return fail(QCoreApplication::translate("CommandLine", "The export cannot replace a file it is made from (%1).").arg(input), 2);
                }
        }
    if (gauges && output.suffix().compare("mov", Qt::CaseInsensitive) != 0 && !ExportSettings::isVp9Alpha(output.suffix()))
        {
            return fail(QCoreApplication::translate("CommandLine", "A .%1 file cannot hold a see-through picture: save the gauges as .mov or .webm.").arg(output.suffix()), 2);
        }
    if (gauges && overlay.regions(ExportPlan::outputSizeFor(info.size, settings.height)).isEmpty())
        {
            return fail(QCoreApplication::translate("CommandLine", "There are no gauges on the video to export."), 2);
        }
    if (!gauges && ExportSettings::needsMatroska(settings.audio) && ExportSettings::isMp4Family(output.suffix()))
        {
            return fail(QCoreApplication::translate("CommandLine", "A .%1 file cannot hold Opus or Vorbis sound: save it as .mkv, or take --audio aac.")
                        .arg(output.suffix()), 2);
        }

    // one export, `one` into `file`: the exit code
    auto exportOne = [&](const ExportSettings &one, const QFileInfo &file) -> int
    {
        say(QCoreApplication::translate("CommandLine", "%1 -> %2").arg(video.fileName(), file.filePath()));
        say(QCoreApplication::translate("CommandLine", "Profile: %1; %2").arg(profile->name, gauges ? QCoreApplication::translate("CommandLine", "the gauges alone, see-through") : describe(one)));
        const ExportPlan plan = ExportPlan::make(project.videoPath, info, one, overlay, project.cuts, soundtrack);
        ExportJob job(ffmpeg, plan, overlay, project.sync(), nullptr, own ? ExportJob::OwnEngine : ExportJob::ProgramEngine);
        const bool quiet = parser.isSet("quiet");
        QEventLoop loop;
        ExportJob::Outcome outcome = ExportJob::Failed;
        QString message;
        QElapsedTimer clockRunning;
        clockRunning.start();
        // in a terminal one line kept up to date a few times a second; into a file or another program
        // a line every tenth
    #ifdef Q_OS_WIN
        const bool terminal = _isatty(_fileno(stderr));
    #else
        const bool terminal = isatty(fileno(stderr));
    #endif
        QElapsedTimer sinceShown;
        int tenthShown = -1;
        bool doneShown = false;
        QObject::connect(&job, &ExportJob::progress, &job, [&](qint64 frame, qint64 total, double fps)
        {
            if (quiet || total <= 0 || doneShown)
                {
                    return;
                }
            const bool done = frame >= total;
            const double part = std::min(1.0, double(frame) / total);
            const int tenth = int(part * 10.0);
            if (terminal ? (!done && sinceShown.isValid() && sinceShown.elapsed() < 250) : (tenth == tenthShown && !done))
                {
                    return;
                }
            sinceShown.start();
            tenthShown = tenth;
            doneShown = done;
            const double elapsed = clockRunning.elapsed() / 1000.0;
            const double left = part > 0.01 ? elapsed / part - elapsed : 0.0;
            std::fprintf(stderr, terminal ? "\r%5.1f %%  %lld / %lld  %.1f fps  %s %s   " : "%5.1f %%  %lld / %lld  %.1f fps  %s %s\n",
                         100.0 * part, (long long)frame, (long long)total, fps, qPrintable(QCoreApplication::translate("CommandLine", "left")), qPrintable(hms(left)));
            std::fflush(stderr);
        });
        QObject::connect(&job, &ExportJob::finished, &job, [&](ExportJob::Outcome how, const QString &why)
        {
            outcome = how;
            message = why;
            loop.quit();
        });
        // Ctrl+C ends it early with a playable file, as Stop in the window does
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, &job, [&]()
        {
            if (interrupted)
                {
                    job.stop();
                }
        });
        watch.start(200);
        QTimer::singleShot(0, &job, &ExportJob::start);
        loop.exec();
        if (!quiet && terminal)
            {
                std::fprintf(stderr, "\n");
            }
        switch (outcome)
            {
            case ExportJob::Succeeded:
                say(QCoreApplication::translate("CommandLine", "Done in %1: %2").arg(hms(clockRunning.elapsed() / 1000.0), file.filePath()));
                return 0;
            case ExportJob::Stopped:
                say(QCoreApplication::translate("CommandLine", "Stopped; what was done so far is in %1").arg(file.filePath()));
                return 130;
            case ExportJob::Canceled:
                return 130;
            case ExportJob::Failed:
                break;
            }
        return fail(message.isEmpty() ? QCoreApplication::translate("CommandLine", "The export failed.") : message);
    };
    if (!parser.isSet("laps"))
        {
            return exportOne(settings, output);
        }

    // one video per lap: each in its own file, named with its number
    if (parser.isSet("from") || parser.isSet("to"))
        {
            return fail(QCoreApplication::translate("CommandLine", "--laps and --from or --to do not go together: the laps are the parts."), 2);
        }
    const QVector<TimeRange> lapRanges = session ? LapExport::ranges(*session, project.sync(), info.duration) : QVector<TimeRange>();
    QVector<int> laps;
    QString error;
    if (lapRanges.isEmpty())
        {
            return fail(QCoreApplication::translate("CommandLine", "The data has no laps: place a start/finish line in the window, "
                                                    "or use data whose device recorded laps."), 2);
        }
    if (!LapExport::numbers(parser.value("laps"), int(lapRanges.size()), &laps, &error))
        {
            return fail(error, 2);
        }
    laps.erase(std::remove_if(laps.begin(), laps.end(), [&lapRanges](int lap) { return lapRanges.at(lap - 1).length() <= 0.0; }),
               laps.end());
    if (laps.isEmpty())
        {
            return fail(QCoreApplication::translate("CommandLine", "The video shows none of these laps."), 2);
        }
    for (int lap : std::as_const(laps))
        {
            const QFileInfo file(LapExport::outputPath(output.filePath(), lap, int(lapRanges.size())));
            if (file.exists() && !parser.isSet("overwrite"))
                {
                    return fail(QCoreApplication::translate("CommandLine", "%1 is there already; --overwrite replaces it.").arg(file.filePath()), 2);
                }
        }
    for (int i = 0; i < laps.size(); ++i)
        {
            const int lap = laps.at(i);
            ExportSettings one = settings;
            one.start = lapRanges.at(lap - 1).start;
            one.duration = lapRanges.at(lap - 1).length();
            one.outputPath = LapExport::outputPath(output.filePath(), lap, int(lapRanges.size()));
            say(QCoreApplication::translate("CommandLine", "Lap %1 (%2 of %3)").arg(lap).arg(i + 1).arg(laps.size()));
            const int code = exportOne(one, QFileInfo(one.outputPath));
            if (code != 0)
                {
                    return code;
                }
        }
    if (laps.size() > 1)
        {
            say(QCoreApplication::translate("CommandLine", "%1 videos, one per lap.").arg(laps.size()));
        }
    return 0;
}
}

bool CommandLine::wanted(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
        {
            for (const char *command : kCommands)
                {
                    if (std::strcmp(argv[i], command) == 0)
                        {
                            return true;
                        }
                }
        }
    return false;
}

void CommandLine::attachConsole()
{
#ifdef Q_OS_WIN
    // a program for the desktop gets no console; the one of cmd or PowerShell it was started from
    // is taken, so that what it says shows there
    if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            std::freopen("CONOUT$", "w", stdout);
            std::freopen("CONOUT$", "w", stderr);
        }
#endif
}

int CommandLine::run()
{
    std::signal(SIGINT, onInterrupt);
    std::signal(SIGTERM, onInterrupt);
    // Qt's own messages (fonts, plugins) only with --verbose: they would bury the progress
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.contains("--verbose"))
        {
            // onto the terminal, also when it is a pipe or a file (Qt would send them to the journal then)
            qInstallMessageHandler([](QtMsgType, const QMessageLogContext &context, const QString &text)
            {
                const bool named = context.category && qstrcmp(context.category, "default") != 0;
                std::fprintf(stderr, "%s%s%s\n", named ? context.category : "", named ? ": " : "", qPrintable(text));
            });
        }
    else
        {
            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &text)
            {
                if (type == QtCriticalMsg || type == QtFatalMsg)
                    {
                        std::fprintf(stderr, "%s\n", qPrintable(text));
                    }
            });
        }

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("CommandLine", 
        "Race Track Visualizer: gauges over a video, exported here without the window.\n"
        "\n"
        "Examples:\n"
        "  trackrideviz --render ride.rtvproj\n"
        "  trackrideviz --render ride.rtvproj -o ride_youtube.mp4 --profile youtube --hardware\n"
        "  trackrideviz --render --video GX010042.MP4 --data ride.fit --template \"Track day\"\n"
        "  trackrideviz --render ride.rtvproj --gauges-only -o gauges.mov\n"
        "  trackrideviz --list-encoders\n"
        "\n"
        "A project is exported as it was saved: its gauges, synchronization, cuts and music. Without one,\n"
        "the video and the data are put in step by their clocks (as when they are opened in the window),\n"
        "or by --offset. Units, maps, the rider and the bike are those of the program's settings.\n"
        "\n"
        "How it is encoded: the export profile the export dialog used last, or --profile; its encoder is\n"
        "the best for the profile among those that work on this computer, of the kind used last (graphics\n"
        "card or processor) or of the kind --hardware / --software asks for; --encoder names one exactly.\n"
        "What follows changes the profile's choices one by one.\n"
        "\n"
        "Exit codes: 0 done, 1 failed, 2 wrong arguments, 130 stopped (Ctrl+C keeps a playable file)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("project", QCoreApplication::translate("CommandLine", "The project to export (as --project)."), "[project.rtvproj]");
    const QList<QCommandLineOption> options
    {
        {"render", QCoreApplication::translate("CommandLine", "Export a video with the gauges.")},
        {"list-encoders", QCoreApplication::translate("CommandLine", "The encoders that work on this computer: of the graphics card and of the processor.")},
        {"list-profiles", QCoreApplication::translate("CommandLine", "The export profiles: the program's and those saved in the export dialog.")},
        {"list-templates", QCoreApplication::translate("CommandLine", "The templates of the library.")},
        {"project", QCoreApplication::translate("CommandLine", "The project to export."), "file"},
        {"video", QCoreApplication::translate("CommandLine", "The video (in place of the project's)."), "file"},
        {"background", QCoreApplication::translate("CommandLine", "No video: the gauges over a plain color as long as the data. "
                                        "A size, then a frame rate and a color if wanted: 1920x1080@30,#202020; "
                                        "\"transparent\" for the gauges alone."), "picture"},
        {"data", QCoreApplication::translate("CommandLine", "The data: .fit, .gpx, .tcx, .csv, NMEA, VBO, or a GoPro video's own (in place of the project's)."), "file"},
        {"template", QCoreApplication::translate("CommandLine", "The gauges of this template of the library, placed for the video's shape (in place of the project's)."), "name"},
        {"offset", QCoreApplication::translate("CommandLine", "Data time = video time + this many seconds (in place of the synchronization)."), "seconds"},
        {"timelapse", QCoreApplication::translate("CommandLine", "A time-lapse video: this many seconds of the data in each second of it (10, 30...)."), "factor"},
        {QStringList{"o", "output"}, QCoreApplication::translate("CommandLine", "The file to write; its suffix (.mp4, .mkv, .mov, .webm) is the container. "
                                        "Without it: beside the video, named <video>_overlay.<suffix>."), "file"},
        {QStringList{"y", "overwrite"}, QCoreApplication::translate("CommandLine", "Replace the output file when it is there.")},
        {"profile", QCoreApplication::translate("CommandLine", "youtube, small, source, or one saved in the export dialog (--list-profiles)."), "name"},
        {"engine", QCoreApplication::translate("CommandLine", "builtin: FFmpeg's libraries built into the program; ffmpeg: the ffmpeg program. "
                                        "Without it: as in the settings (built in when the program has it)."), "engine"},
        {"encoder", QCoreApplication::translate("CommandLine", "This encoder exactly, as --list-encoders names it: libx264, h264_nvenc, hevc_vaapi..."), "name"},
        {"hardware", QCoreApplication::translate("CommandLine", "An encoder of the graphics card for the profile's codec.")},
        {"software", QCoreApplication::translate("CommandLine", "An encoder of the processor for the profile's codec.")},
        {"bitrate", QCoreApplication::translate("CommandLine", "Video bitrate in kbit/s."), "kbps"},
        {"quality", QCoreApplication::translate("CommandLine", "Constant quality instead of a bitrate, on the encoder's own scale (lower is better)."), "number"},
        {"speed", QCoreApplication::translate("CommandLine", "fast, balanced or small: encoding time against file size."), "speed"},
        {"height", QCoreApplication::translate("CommandLine", "Output height in pixels (never larger than the video); 0: as the video."), "pixels"},
        {"audio", QCoreApplication::translate("CommandLine", "copy, aac, none, opus or vorbis (opus and vorbis go into .mkv)."), "sound"},
        {"audio-bitrate", QCoreApplication::translate("CommandLine", "Sound bitrate in kbit/s, when the sound is encoded again."), "kbps"},
        {"gauges-only", QCoreApplication::translate("CommandLine", "The gauges alone on a see-through background, for another editor: .mov (ProRes 4444) or .webm.")},
        {"from", QCoreApplication::translate("CommandLine", "Start this many seconds into the video."), "seconds"},
        {"to", QCoreApplication::translate("CommandLine", "End this many seconds into the video."), "seconds"},
        {"laps", QCoreApplication::translate("CommandLine", "One video per lap, named <output>_lapNN: all, or laps as 3 or 1,4-6."), "laps"},
        {"faster", QCoreApplication::translate("CommandLine", "The output plays this many times faster, without the camera's sound (2, 4, 10...)."), "factor"},
        {QStringList{"q", "quiet"}, QCoreApplication::translate("CommandLine", "No progress, only the outcome.")},
        {"verbose", QCoreApplication::translate("CommandLine", "Qt's own messages too, for finding out what goes wrong.")},
    };
    parser.addOptions(options);
    parser.process(*QCoreApplication::instance());

    if (parser.isSet("list-profiles"))
        {
            return listProfiles();
        }
    if (parser.isSet("list-templates"))
        {
            return listTemplates();
        }
    // the engine wanted
    bool preferOwn = AppSettings::ownVideoEngine();
    if (parser.isSet("engine"))
        {
            const QString engine = parser.value("engine").toLower();
            if (engine != QLatin1String("builtin") && engine != QLatin1String("ffmpeg"))
                {
                    return fail(QCoreApplication::translate("CommandLine", "--engine takes builtin or ffmpeg, not %1.").arg(engine), 2);
                }
            preferOwn = engine == QLatin1String("builtin");
            if (preferOwn && !Libav::available())
                {
                    return fail(QCoreApplication::translate("CommandLine", "This build has no engine of its own: it exports with the ffmpeg program."), 2);
                }
        }
    if (parser.isSet("list-encoders"))
        {
            return listEncoders(preferOwn);
        }
    if (parser.isSet("render"))
        {
            return render(parser, preferOwn);
        }
    parser.showHelp(2);
}
