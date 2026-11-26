#include "simplecontroller.h"

#include "applog.h"
#include "gauge/gaugelibrary.h"
#include "laps/lapengine.h"
#include "maps/tilestore.h"
#include "overlay/gaugetemplate.h"
#include "overlay/templatelibrary.h"
#include "perfmeter.h"
#include "project/projectdata.h"
#include "settings/appsettings.h"
#include "sync/videosync.h"
#include "telemetry/datasources.h"
#include "telemetry/demosession.h"
#include "telemetry/powerestimate.h"
#include "telemetry/telemetryimporter.h"
#include "units/units.h"
#include "video/exportjob.h"
#include "video/exportplan.h"
#include "video/exportprofile.h"
#include "video/ffmpegtools.h"
#include "video/libav.h"
#include "weather/weather.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>

namespace
{
// the template a first video gets: the shipped one for any ride
const char kFirstTemplate[] = "Ride";
// the gauge a ride without a video is shown with, the whole picture: its route in 3D
const char kFlyoverGauge[] = "bundled:Route map arrow";
const char kFlyoverMap[] = "satellite";
const double kFlyoverCloseUp = 300.0;   // meters across
// a ride without a video: a picture this big and fast over the data
const QSize kBackgroundSize(1920, 1080);
const int kBackgroundFps = 30;
// how far around the line placed the best place for it is looked for, meters
const double kLineSearchRadius = 100.0;
// how long the heights of the terrain and the weather are waited for, a look every two seconds
const int kArrivalLookMs = 2000;
const int kArrivalTries = 30;

struct Loaded
{
    TelemetrySessionPtr raw;
    TelemetrySessionPtr worked;     // before its laps
    TelemetrySessionPtr session;
    QString error;
    bool waiting{false};
};

struct Probed
{
    bool ok{false};
    MediaInfo info;
    QString error;
};

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// The other way: a file's path, or an address Android handed over (content://), as a URL.
QUrl urlOf(const QString &path)
{
    return path.startsWith(QLatin1String("content:")) ? QUrl(path) : QUrl::fromLocalFile(path);
}

QVector<ExportProfile> allProfiles()
{
    return ExportProfiles::builtIn() + AppSettings::exportProfiles();
}

// The data worked out as the window does it: the user's choices of the settings, laps found.
Loaded workOut(const TelemetrySessionPtr &raw, const Project &project)
{
    Loaded result;
    result.raw = raw;
    bool weatherWaiting = false;
    const TelemetrySessionPtr worked = ProjectData::workedOut(raw, project.dataEdits, project.elevation, AppSettings::powerRider(),
                                       project.surfaces, AppSettings::weather(), project.wind,
                                       AppSettings::drivetrain(), &result.waiting, &weatherWaiting);
    result.waiting = result.waiting || weatherWaiting;
    result.worked = worked;
    result.session = worked ? LapEngine::apply(worked, project.startFinish, project.sectors, project.referenceLap) : worked;
    return result;
}

// the largest a gauge gets, a fraction of the picture's height: a little more than the picture, as
// the route in 3D under a dash is
const double kMaxGaugeHeight = 1.5;

// Where a gauge `size` long (a fraction of the picture) starts so that it stays on the picture;
// one longer than the picture covers it instead.
double onPicture(double start, double size)
{
    return std::clamp(start, std::min(0.0, 1.0 - size), std::max(0.0, 1.0 - size));
}

// The route in 3D under the gauges of a ride without a video: the whole picture and more.
bool isBackdrop(const OverlayItem &item)
{
    return item.gaugeId == QLatin1String(kFlyoverGauge) && item.flyover == 1 && item.height > 1.0;
}

// What a file is, by its name.
enum class FileKind
{
    Project,
    Video,
    Music,
    Data,
};

FileKind kindOf(const QString &path)
{
    static const QStringList videos{"mp4", "mov", "mkv", "avi", "insv", "lrv", "m4v", "webm"};
    static const QStringList music{"mp3", "m4a", "aac", "ogg", "opus", "flac", "wav"};
    const QString suffix = QFileInfo(path).suffix().toLower();
    // an address with no name in it (Android's content://): a video by its first bytes
    if (!videos.contains(suffix) && !music.contains(suffix) && suffix != QLatin1String(ProjectFile::Suffix))
        {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly) && file.read(8).mid(4, 4) == "ftyp")
                {
                    return FileKind::Video;
                }
        }
    if (suffix == QLatin1String(ProjectFile::Suffix))
        {
            return FileKind::Project;
        }
    if (videos.contains(suffix))
        {
            return FileKind::Video;
        }
    return music.contains(suffix) ? FileKind::Music : FileKind::Data;
}

// The colors of a theme, in the order the properties show them.
QColor GaugeTheme::*const kColorRoles[] = {&GaugeTheme::foreground, &GaugeTheme::background, &GaugeTheme::accent,
                                            &GaugeTheme::accent2};
// how far off the route a tap on the map may be and still mean it, meters
const double kTapOffRoute = 60.0;
// how many of each kind the first screen and the menu offer
const int kRecentShown = 6;

QString lapTimeText(double seconds)
{
    const int hundredths = int(std::lround(std::max(0.0, seconds) * 100.0));
    return QString("%1:%2.%3").arg(hundredths / 6000).arg(hundredths / 100 % 60, 2, 10, QChar('0'))
           .arg(hundredths % 100, 2, 10, QChar('0'));
}
}

SimpleController::SimpleController(bool canGoBack, QObject *parent)
    : QObject(parent)
    , m_canGoBack(canGoBack)
    , m_overlay(std::make_shared<const Overlay>())
{
    AppSettings::applyMapSources();
    WeatherStore::instance()->setDownloads(true);
    const QVector<ExportProfile> profiles = allProfiles();
    for (int i = 0; i < profiles.size(); ++i)
        {
            if (profiles.at(i).id == AppSettings::exportProfile())
                {
                    m_profileIndex = i;
                }
        }
    // what comes later (the heights of the terrain, the weather) is had by working the data out again
    m_arrivals.setInterval(kArrivalLookMs);
    connect(&m_arrivals, &QTimer::timeout, this, [this]()
    {
        if (!m_raw || ++m_arrivalTries > kArrivalTries)
            {
                m_arrivals.stop();
                return;
            }
        const Loaded result = workOut(m_raw, m_project);
        if (result.session)
            {
                m_worked = result.worked;
                m_session = result.session;
                rebuildOverlay();
            }
        if (!result.waiting)
            {
                m_arrivals.stop();
            }
    });
    // map tiles that come are drawn
    connect(TileStore::instance(), &TileStore::arrived, this, [this]()
    {
        emit overlayChanged();
    });
}

SimpleController::~SimpleController()
{
    if (m_thread)
        {
            // the job ends with its thread; what it wrote is removed
            m_thread->quit();
            m_thread->wait();
        }
}

QUrl SimpleController::videoUrl() const
{
    return m_project.videoPath.isEmpty() ? QUrl() : urlOf(m_project.videoPath);
}

QString SimpleController::videoName() const
{
    return QFileInfo(m_project.videoPath).fileName();
}

QString SimpleController::dataName() const
{
    return m_demo ? tr("Demo ride") : m_session ? QFileInfo(m_project.telemetryPath).fileName() : QString();
}

qint64 SimpleController::dataDuration() const
{
    return m_session ? qint64(std::llround(m_session->duration() * 1000.0)) : 0;
}

QString SimpleController::projectName() const
{
    return m_projectPath.isEmpty() ? QString() : QFileInfo(m_projectPath).completeBaseName();
}

QUrl SimpleController::projectFolder() const
{
    for (const QString &path : {m_projectPath, m_project.videoPath, m_project.telemetryPath})
        {
            if (!path.isEmpty())
                {
                    return QUrl::fromLocalFile(QFileInfo(path).absolutePath());
                }
        }
    return QUrl::fromLocalFile(QDir::homePath());
}

QStringList SimpleController::channelIds() const
{
    return m_session ? m_session->channelIds() : QStringList();
}

QStringList SimpleController::channelNames() const
{
    QStringList names;
    const Units units = AppSettings::units();
    for (const QString &id : channelIds())
        {
            const Channel *data = m_session->channel(id);
            names << ChannelFormat::displayFor(id, data ? data->unit() : QString(), units).name;
        }
    return names;
}

QVariantList SimpleController::recentFiles() const
{
    QVariantList files;
    const QVector<QPair<AppSettings::RecentKind, QString>> kinds
    {
        {AppSettings::RecentProjects, QStringLiteral("project")}, {AppSettings::RecentVideos, QStringLiteral("video")},
        {AppSettings::RecentData, QStringLiteral("data")},
    };
    for (const auto &kind : kinds)
        {
            int shown = 0;
            for (const QString &path : AppSettings::recentFiles(kind.first))
                {
                    if (shown >= kRecentShown || !QFileInfo::exists(path))
                        {
                            continue;
                        }
                    const QFileInfo info(path);
                    QVariantMap file;
                    file["url"] = QUrl::fromLocalFile(info.absoluteFilePath());
                    file["name"] = info.fileName();
                    file["folder"] = QDir::toNativeSeparators(info.absolutePath());
                    file["kind"] = kind.second;
                    files << file;
                    ++shown;
                }
        }
    return files;
}

QVariantList SimpleController::laps() const
{
    QVariantList laps;
    if (!m_session)
        {
            return laps;
        }
    const QVector<TelemetrySession::Lap> &all = m_session->laps();
    double best = -1.0;
    for (const TelemetrySession::Lap &lap : all)
        {
            const double time = lap.endTime - lap.startTime;
            best = best < 0.0 || time < best ? time : best;
        }
    const TelemetrySession::Lap reference = m_session->referenceLap();
    const double referenceTime = reference.endTime > reference.startTime ? reference.endTime - reference.startTime : best;
    const SyncMap sync = m_project.sync();
    for (int i = 0; i < all.size(); ++i)
        {
            const TelemetrySession::Lap &lap = all.at(i);
            const double time = lap.endTime - lap.startTime;
            const double delta = time - referenceTime;
            QVariantMap row;
            row["number"] = i + 1;
            row["start"] = qint64(std::llround(std::max(0.0, sync.videoTime(lap.startTime)) * 1000.0));
            row["time"] = lapTimeText(time);
            row["delta"] = std::abs(delta) < 0.005 ? QString() : (delta > 0.0 ? "+" : "−") + QString::number(std::abs(delta), 'f', 2);
            row["best"] = time == best;
            row["reference"] = m_project.referenceLap > 0 ? m_project.referenceLap == i + 1 : time == best;
            laps << row;
        }
    return laps;
}

QStringList SimpleController::templateNames() const
{
    QStringList names;
    for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
        {
            names << entry.name;
        }
    return names;
}

QStringList SimpleController::profileNames() const
{
    QStringList names;
    for (const ExportProfile &profile : allProfiles())
        {
            names << profile.name;
        }
    return names;
}

QStringList SimpleController::templateIds() const
{
    QStringList ids;
    for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
        {
            ids << entry.id;
        }
    return ids;
}

QStringList SimpleController::gaugeNames() const
{
    QStringList names;
    for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
        {
            names << entry.name;
        }
    return names;
}

QStringList SimpleController::gaugeIds() const
{
    QStringList ids;
    for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
        {
            ids << entry.id;
        }
    return ids;
}

QStringList SimpleController::musicNames() const
{
    QStringList names;
    for (const MusicTrack &track : m_project.soundtrack.tracks)
        {
            names << QFileInfo(track.path).fileName();
        }
    return names;
}

void SimpleController::setVideoVolume(int percent)
{
    percent = std::clamp(percent, 0, Soundtrack::kMaxVolume);
    if (percent != m_project.soundtrack.videoVolume)
        {
            m_project.soundtrack.videoVolume = percent;
            setModified();
            emit musicChanged();
        }
}

void SimpleController::setMusicVolume(int percent)
{
    percent = std::clamp(percent, 0, Soundtrack::kMaxVolume);
    if (percent != m_project.soundtrack.musicVolume)
        {
            m_project.soundtrack.musicVolume = percent;
            setModified();
            emit musicChanged();
        }
}

void SimpleController::setProfileIndex(int index)
{
    if (index == m_profileIndex || index < 0 || index >= allProfiles().size())
        {
            return;
        }
    m_profileIndex = index;
    AppSettings::setExportProfile(allProfiles().at(index).id);
    emit profileChanged();
}

double SimpleController::aspect() const
{
    return m_info.size.height() > 0 ? double(m_info.size.width()) / m_info.size.height() : 16.0 / 9.0;
}

QUrl SimpleController::exportedUrl() const
{
    return m_exportedPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_exportedPath);
}

double SimpleController::telemetryTime(qint64 videoMs) const
{
    return m_project.sync().telemetryTime(videoMs / 1000.0);
}

void SimpleController::setMessage(const QString &message)
{
    if (message != m_message)
        {
            m_message = message;
            emit messageChanged();
        }
}

void SimpleController::setBusy(bool more)
{
    m_loading += more ? 1 : -1;
    emit busyChanged();
}

void SimpleController::openVideo(const QUrl &url)
{
    const QString path = localPath(url);
    if (path.isEmpty())
        {
            return;
        }
    m_project.videoPath = path;
    AppSettings::addRecentFile(AppSettings::RecentVideos, path);
    emit recentChanged();
    setModified();
    // what was cut, the breaks between joined recordings, a time-lapse's rate and the step
    // belong to the video before
    m_project.videoShifts.clear();
    m_project.cuts.clear();
    m_project.syncRate = 1.0;
    m_project.syncIsManual = false;
    probeVideo(path);
}

void SimpleController::probeVideo(const QString &path)
{
    m_probed = false;
    m_info = MediaInfo();
    emit videoChanged();
    setMessage(tr("Opening %1...").arg(QFileInfo(path).fileName()));

    // its size and length, for the layout of the gauges, the clocks and the export
    setBusy(true);
    auto *watcher = new QFutureWatcher<Probed>(this);
    connect(watcher, &QFutureWatcher<Probed>::finished, this, [this, watcher, path]()
    {
        const Probed probed = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (path != m_project.videoPath)
            {
                return;
            }
        m_probed = probed.ok;
        m_info = probed.info;
        if (!probed.ok)
            {
                setMessage(probed.error);
                return;
            }
        // the picture is the video's now
        m_project.background = VideoBackground();
        emit videoChanged();
        setMessage(QString());
        if (m_session)
            {
                // the gauges laid out for its shape, and in step by the clocks
                firstGauges();
                rebuildOverlay();
                followClocks();
            }
        // a camera that records its own data (GoPro): read out of the video, in place of the demo
        // too (until then the demo stays over it)
        if (!m_fileDataComing && (!m_session || m_demo))
            {
                loadData(path, true);
            }
    });
    const bool preferOwn = AppSettings::ownVideoEngine();
    watcher->setFuture(QtConcurrent::run([path, preferOwn]()
    {
        Probed result;
        result.ok = MediaProbe::probe(FfmpegTools::ffprobeProgram(), path, &result.info, &result.error, preferOwn);
        return result;
    }));
}

void SimpleController::openData(const QUrl &url)
{
    const QString path = localPath(url);
    if (!path.isEmpty())
        {
            // corrections, added files, surfaces, the reference lap and the step belong to the data before
            m_project.dataEdits.clear();
            m_project.moreData.clear();
            m_project.surfaces.clear();
            m_project.referenceLap = 0;
            m_project.syncIsManual = false;
            setModified();
            loadData(path, false);
        }
}

void SimpleController::openFiles(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls)
        {
            const QString path = localPath(url);
            if (!path.isEmpty())
                {
                    paths << path;
                }
        }
    // a project is all of it: the rest is not mixed in
    for (const QString &path : std::as_const(paths))
        {
            if (kindOf(path) == FileKind::Project)
                {
                    Project project;
                    QString error;
                    if (!ProjectFile::load(path, &project, &error))
                        {
                            setMessage(error);
                            return;
                        }
                    openProject(project, path);
                    return;
                }
        }
    QStringList videos;
    for (const QString &path : std::as_const(paths))
        {
            if (kindOf(path) == FileKind::Video)
                {
                    videos << path;
                }
        }
    // several videos: made one first, if wanted; a camera's chapter: the ones after it too
    const QStringList chapters = videos.size() == 1 ? VideoJoin::chaptersFrom(videos.first()) : QStringList();
    if (videos.size() >= 2)
        {
            prepareJoin(VideoJoin::ordered(videos), false);
        }
    else if (chapters.size() >= 2)
        {
            prepareJoin(chapters, true);
        }
    else if (!videos.isEmpty())
        {
            // the video first: data of its own is then not looked for when a file of it comes too
            openVideo(urlOf(videos.first()));
        }
    for (const QString &path : std::as_const(paths))
        {
            switch (kindOf(path))
                {
                case FileKind::Data:
                    openData(urlOf(path));
                    break;
                case FileKind::Music:
                    addMusic(urlOf(path));
                    break;
                case FileKind::Project:
                case FileKind::Video:
                    break;
                }
        }
}

QStringList SimpleController::joinNames() const
{
    QStringList names;
    for (const VideoJoin::Part &part : m_joinParts)
        {
            names << QFileInfo(part.path).fileName();
        }
    return names;
}

void SimpleController::prepareJoin(const QStringList &paths, bool chapters)
{
    cancelJoin();
    const quint64 generation = ++m_joinGeneration;
    m_joinParts.clear();
    for (const QString &path : paths)
        {
            m_joinParts.append({path, MediaInfo()});
        }
    m_joinChapters = chapters;
    m_joinReady = false;
    m_joinProgress = 0.0;
    m_joinText = tr("Reading the videos...");
    emit joinChanged();
    emit joinOffered();
    auto *watcher = new QFutureWatcher<QPair<QVector<VideoJoin::Part>, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<QVector<VideoJoin::Part>, QString>>::finished, this, [this, watcher, generation]()
    {
        const auto probed = watcher->result();
        watcher->deleteLater();
        if (generation != m_joinGeneration)
            {
                return;
            }
        if (!probed.second.isEmpty())
            {
                m_joinText = probed.second;
                emit joinChanged();
                return;
            }
        m_joinParts = probed.first;
        describeJoin();
        m_joinReady = true;
        emit joinChanged();
    });
    const QString ffprobe = FfmpegTools::ffprobeProgram();
    const bool preferOwn = AppSettings::ownVideoEngine();
    watcher->setFuture(QtConcurrent::run([ffprobe, paths, preferOwn]()
    {
        QPair<QVector<VideoJoin::Part>, QString> probed;
        for (const QString &path : paths)
            {
                VideoJoin::Part part{path, MediaInfo()};
                QString error;
                if (!MediaProbe::probe(ffprobe, path, &part.info, &error, preferOwn))
                    {
                        probed.second = QString("%1: %2").arg(QFileInfo(path).fileName(), error);
                        break;
                    }
                probed.first.append(part);
            }
        return probed;
    }));
}

// What joining the parts in their order means: the breaks between them, the joined file, copied or
// encoded again.
void SimpleController::describeJoin()
{
    m_joinShifts = VideoJoin::shifts(m_joinParts);
    m_joinLength = 0.0;
    for (const VideoJoin::Part &part : std::as_const(m_joinParts))
        {
            m_joinLength += part.info.duration;
        }
    // beside the first, named after it; never over a file that is there
    const QFileInfo first(m_joinParts.first().path);
    m_joinOutput = first.dir().filePath(first.completeBaseName() + "_joined." + first.suffix());
    for (int n = 2; QFileInfo::exists(m_joinOutput); ++n)
        {
            m_joinOutput = first.dir().filePath(QString("%1_joined_%2.%3").arg(first.completeBaseName()).arg(n).arg(first.suffix()));
        }
    QString why = VideoJoin::whyNot(m_joinParts);
    if (why.isEmpty() && !(AppSettings::ownVideoEngine() && Libav::available()))
        {
            why = VideoJoin::whyNotByProgram(m_joinParts);
        }
    m_joinCopies = why.isEmpty();
    const QString length = QString("%1:%2").arg(int(m_joinLength) / 60).arg(int(m_joinLength) % 60, 2, 10, QChar('0'));
    if (m_joinCopies)
        {
            m_joinText = tr("%n videos, %1 long, copied one after another into %2: quick, and nothing is lost.",
                            nullptr, int(m_joinParts.size())).arg(length, QFileInfo(m_joinOutput).fileName());
        }
    else
        {
            const MediaInfo &size = m_joinParts.first().info;
            m_joinText = why + "\n\n" + tr("They can be joined by encoding them again into %1. That takes a while; the "
                                           "joined video gets the size and frame rate of the first one (%2×%3), the others "
                                           "scaled into it.").arg(QFileInfo(m_joinOutput).fileName())
                         .arg(size.size.width()).arg(size.size.height());
            const bool cameraData = std::any_of(m_joinParts.cbegin(), m_joinParts.cend(),
                                                [](const VideoJoin::Part &part) { return part.info.hasCameraData; });
            if (cameraData)
                {
                    m_joinText += " " + tr("The camera's data in them does not come along.");
                }
        }
}

void SimpleController::moveJoinPart(int index, int by)
{
    const int to = index + by;
    if (m_joinJob || !m_joinReady || index < 0 || index >= m_joinParts.size() || to < 0 || to >= m_joinParts.size())
        {
            return;
        }
    m_joinParts.move(index, to);
    describeJoin();
    emit joinChanged();
}

void SimpleController::startJoin()
{
    if (!m_joinReady || m_joinJob || m_joinParts.isEmpty())
        {
            return;
        }
    auto *job = new VideoJoinJob(AppSettings::ownVideoEngine() ? ExportJob::OwnEngine : ExportJob::ProgramEngine, this);
    m_joinJob = job;
    connect(job, &VideoJoinJob::progress, this, [this](double seconds)
    {
        m_joinProgress = m_joinLength > 0.0 ? std::clamp(seconds / m_joinLength, 0.0, 1.0) : 0.0;
        emit joinChanged();
    });
    connect(job, &VideoJoinJob::finished, this, [this, job](bool ok, const QString &error)
    {
        job->deleteLater();
        m_joinJob = nullptr;
        if (!ok)
            {
                m_joinText = tr("Joining failed: %1").arg(error);
                emit joinChanged();
                return;
            }
        const QVector<TimeShift> shifts = m_joinShifts;
        const QString output = m_joinOutput;
        m_joinParts.clear();
        m_joinReady = false;
        m_joinText.clear();
        emit joinChanged();
        openVideo(QUrl::fromLocalFile(output));
        // the breaks between the recordings, for the data to follow them
        m_project.videoShifts = shifts;
        emit syncChanged();
        emit overlayChanged();
    });
    m_joinText = m_joinCopies ? tr("Joining...") : tr("Encoding...");
    m_joinProgress = 0.0;
    if (m_joinCopies)
        {
            QStringList paths;
            for (const VideoJoin::Part &part : std::as_const(m_joinParts))
                {
                    paths << part.path;
                }
            job->startCopy(paths, m_joinOutput, VideoJoin::keepsCameraData(m_joinParts, m_joinOutput));
        }
    else
        {
            // the encoder and rate of the last export, at the first video's bitrate
            ExportSettings settings = AppSettings::hasExportDefaults() ? AppSettings::exportDefaults() : ExportSettings();
            if (m_joinParts.first().info.bitrateKbps > 0)
                {
                    settings.bitrateKbps = m_joinParts.first().info.bitrateKbps;
                }
            job->startEncode(m_joinParts, m_joinOutput, VideoEncoders::byId(settings.encoder), settings);
        }
    emit joinChanged();
}

void SimpleController::cancelJoin()
{
    ++m_joinGeneration;
    if (m_joinJob)
        {
            m_joinJob->cancel();
            m_joinJob->deleteLater();
            m_joinJob = nullptr;
        }
    m_joinParts.clear();
    m_joinReady = false;
    m_joinText.clear();
    m_joinProgress = 0.0;
    emit joinChanged();
}

bool SimpleController::weather() const
{
    return AppSettings::weather().enabled;
}

void SimpleController::setWeather(bool fetched)
{
    WeatherChoice choice = AppSettings::weather();
    if (choice.enabled == fetched)
        {
            return;
        }
    choice.enabled = fetched;
    AppSettings::setWeather(choice);
    emit weatherChanged();
    // the data worked out again with it, or without it, as the weather comes
    if (m_raw)
        {
            m_arrivalTries = 0;
            m_arrivals.start();
        }
}

QVariantMap SimpleController::rider() const
{
    const PowerEstimate::Rider rider = AppSettings::powerRider();
    return {{"enabled", rider.enabled}, {"riderKg", rider.riderKg}, {"heightCm", rider.heightCm},
        {"bikeKg", rider.bikeKg}, {"tires", rider.tires}, {"position", rider.position}};
}

void SimpleController::setRider(const QVariantMap &changes)
{
    PowerEstimate::Rider rider = AppSettings::powerRider();
    const PowerEstimate::Rider before = rider;
    rider.enabled = changes.value("enabled", rider.enabled).toBool();
    rider.riderKg = std::clamp(changes.value("riderKg", rider.riderKg).toDouble(), 20.0, 250.0);
    rider.heightCm = std::clamp(changes.value("heightCm", rider.heightCm).toDouble(), 100.0, 230.0);
    rider.bikeKg = std::clamp(changes.value("bikeKg", rider.bikeKg).toDouble(), 1.0, 60.0);
    const QString tires = changes.value("tires", rider.tires).toString();
    if (tires == PowerEstimate::OwnValue || PowerEstimate::tires().contains(tires))
        {
            rider.tires = tires;
        }
    const QString position = changes.value("position", rider.position).toString();
    if (position == PowerEstimate::OwnValue || PowerEstimate::positions().contains(position))
        {
            rider.position = position;
        }
    if (rider == before)
        {
            return;
        }
    AppSettings::setPowerRider(rider);
    emit riderChanged();
    if (m_raw)
        {
            m_arrivalTries = 0;
            m_arrivals.start();
        }
}

void SimpleController::setGaugesOnly(bool only)
{
    if (only != m_gaugesOnly)
        {
            m_gaugesOnly = only;
            emit profileChanged();
        }
}

QString SimpleController::timings() const
{
    return PerfMeter::summary();
}

void SimpleController::copyLog() const
{
    QGuiApplication::clipboard()->setText(AppLog::text());
}

void SimpleController::openDemo()
{
    if (m_thread)
        {
            return;
        }
    ++m_dataGeneration;
    m_fileDataComing = false;
    m_arrivals.stop();
    m_demo = true;
    m_project.telemetryPath.clear();
    m_project.dataEdits.clear();
    m_project.moreData.clear();
    m_project.surfaces.clear();
    m_project.startFinish = StartFinishLine();
    m_project.sectors.clear();
    m_project.referenceLap = 0;
    m_raw = DemoSession::create();
    m_worked = m_raw;
    m_session = LapEngine::apply(m_worked, m_project.startFinish, m_project.sectors, m_project.referenceLap);
    useBackground();
    emit dataChanged();
    emit lapsChanged();
    emit projectChanged();
    setMessage(QString());
    firstGauges();
    rebuildOverlay();
}

bool SimpleController::save()
{
    if (m_projectPath.isEmpty() || !canSave())
        {
            return false;
        }
    QString error;
    if (!ProjectFile::save(m_projectPath, m_project, &error))
        {
            setMessage(error);
            return false;
        }
    setModified(false);
    return true;
}

bool SimpleController::saveAs(const QUrl &url)
{
    QString path = localPath(url);
    if (path.isEmpty())
        {
            return false;
        }
    if (QFileInfo(path).suffix().compare(QLatin1String(ProjectFile::Suffix), Qt::CaseInsensitive) != 0)
        {
            path += QLatin1Char('.') + QLatin1String(ProjectFile::Suffix);
        }
    m_projectPath = path;
    if (!save())
        {
            return false;
        }
    AppSettings::addRecentFile(AppSettings::RecentProjects, path);
    emit recentChanged();
    emit projectChanged();
    return true;
}

void SimpleController::newProject()
{
    if (m_thread)
        {
            return;
        }
    ++m_dataGeneration;
    m_fileDataComing = false;
    m_arrivals.stop();
    m_project = Project();
    m_info = MediaInfo();
    m_probed = false;
    m_raw.reset();
    m_worked.reset();
    m_session.reset();
    m_demo = false;
    m_templateIndex = -1;
    m_projectPath.clear();
    m_modified = false;
    m_syncText.clear();
    m_exportText.clear();
    m_exportedPath.clear();
    m_message.clear();
    rebuildOverlay();
    emit videoChanged();
    emit dataChanged();
    emit syncChanged();
    emit musicChanged();
    emit lapsChanged();
    emit exportChanged();
    emit messageChanged();
    emit projectChanged();
}

void SimpleController::setModified(bool modified)
{
    m_modified = modified;
    emit projectChanged();
}

void SimpleController::useBackground()
{
    if (!m_project.videoPath.isEmpty() || !m_session)
        {
            return;
        }
    // the picture runs on the data's own time: nothing to match
    m_project.background.size = kBackgroundSize;
    m_project.background.fps = kBackgroundFps;
    m_project.background.color = Qt::black;
    m_info = m_project.background.mediaInfo(m_session->duration());
    m_probed = true;
    m_project.videoShifts.clear();
    m_project.syncRate = 1.0;
    m_project.syncIsManual = false;
    setSync(0.0, tr("No video: the gauges follow the data's own time."));
    emit videoChanged();
}

QVector<OverlayItem> SimpleController::flyoverItems() const
{
    OverlayItem item;
    item.gaugeId = QLatin1String(kFlyoverGauge);
    item.height = 1.0;
    item.aspect = aspect();
    // its map the whole picture, the margin around it off the edges
    const Gauge gauge = GaugeLibrary::gauge(item.gaugeId);
    if (!gauge.components.isEmpty() && gauge.size.width() > 0.0 && gauge.size.height() > 0.0)
        {
            const QRectF map = gauge.components.first().frame;
            const double across = map.width() / gauge.size.width();
            const double down = map.height() / gauge.size.height();
            if (across > 0.0 && down > 0.0)
                {
                    item.height = 1.0 / down;
                    item.y = -map.top() / gauge.size.height() * item.height;
                    item.x = -map.left() / gauge.size.width() / across;
                    item.aspect = aspect() * down / across;
                }
        }
    item.flyover = 1;
    item.closeUp = kFlyoverCloseUp;
    item.headingUp = 1;
    item.streets = OverlayItem::Shown;
    for (const TileStore::Provider &provider : AppSettings::usableMapProviders())
        {
            item.map = provider.id == QLatin1String(kFlyoverMap) ? provider.id : item.map;
        }
    return {item};
}

void SimpleController::firstGauges()
{
    const bool usual = m_project.overlay == Overlay::defaultItems();
    if (m_project.videoPath.isEmpty())
        {
            if (usual)
                {
                    m_project.overlay = flyoverItems();
                    m_project.layoutAspect = aspect();
                    m_templateIndex = -1;
                }
            return;
        }
    // over a video the picture is the video's: the route in 3D under the gauges goes
    m_project.overlay.erase(std::remove_if(m_project.overlay.begin(), m_project.overlay.end(), isBackdrop),
                            m_project.overlay.end());
    if (!usual && !m_project.overlay.isEmpty())
        {
            return;
        }
    // nothing of the user's: a dash
    const QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
    int first = entries.isEmpty() ? -1 : 0;
    for (int i = 0; i < entries.size(); ++i)
        {
            if (entries.at(i).name.compare(QLatin1String(kFirstTemplate), Qt::CaseInsensitive) == 0)
                {
                    first = i;
                }
        }
    if (first >= 0)
        {
            applyTemplate(first);
        }
}

void SimpleController::applyLaps()
{
    if (!m_worked)
        {
            return;
        }
    m_session = LapEngine::apply(m_worked, m_project.startFinish, m_project.sectors, m_project.referenceLap);
    setModified();
    rebuildOverlay();
    emit lapsChanged();
}

bool SimpleController::lineHere(qint64 videoMs)
{
    if (!m_worked)
        {
            return false;
        }
    const double time = telemetryTime(videoMs);
    // as wide as the other passes there need: a lap ridden a little apart is still crossed
    const StartFinishLine line = LapEngine::lineAt(*m_worked, time, LapEngine::lineWidthAt(*m_worked, time));
    if (!line.isValid())
        {
            return false;
        }
    m_project.startFinish = line;
    applyLaps();
    return true;
}

bool SimpleController::placeLine(double lat, double lon)
{
    if (!m_worked)
        {
            return false;
        }
    double distance = 0.0;
    const double time = LapEngine::nearestTime(*m_worked, lat, lon, &distance);
    // a tap well off the route is not meant for it
    if (time < 0.0 || distance > kTapOffRoute)
        {
            return false;
        }
    // as wide as the other passes there need: a lap ridden a little apart is still crossed
    const StartFinishLine line = LapEngine::lineAt(*m_worked, time, LapEngine::lineWidthAt(*m_worked, time));
    if (!line.isValid())
        {
            return false;
        }
    m_project.startFinish = line;
    applyLaps();
    return true;
}

bool SimpleController::findBestLine()
{
    if (!m_worked || !m_project.startFinish.isValid())
        {
            return false;
        }
    const StartFinishLine line = LapEngine::findLine(*m_worked, m_project.startFinish, kLineSearchRadius);
    if (!line.isValid())
        {
            return false;
        }
    m_project.startFinish = line;
    applyLaps();
    return true;
}

void SimpleController::clearLine()
{
    m_project.startFinish = StartFinishLine();
    applyLaps();
}

void SimpleController::setReferenceLap(int number)
{
    m_project.referenceLap = std::max(0, number);
    applyLaps();
}

void SimpleController::openProject(const Project &project, const QString &path)
{
    newProject();
    m_project = project;
    m_projectPath = path;
    if (!path.isEmpty())
        {
            AppSettings::addRecentFile(AppSettings::RecentProjects, path);
            emit recentChanged();
        }
    m_templateIndex = -1;
    m_syncText = project.syncIsManual ? tr("Moved by hand.") : QString();
    emit syncChanged();
    emit musicChanged();
    rebuildOverlay();
    // the data first: the video's own is then not looked for
    if (!m_project.telemetryPath.isEmpty())
        {
            loadData(m_project.telemetryPath, false);
        }
    if (!m_project.videoPath.isEmpty())
        {
            probeVideo(m_project.videoPath);
        }
    emit projectChanged();
}

void SimpleController::loadData(const QString &path, bool fromVideo)
{
    // only the last load counts
    const quint64 generation = ++m_dataGeneration;
    m_fileDataComing = !fromVideo;
    if (!fromVideo)
        {
            setMessage(tr("Reading %1...").arg(QFileInfo(path).fileName()));
        }
    setBusy(true);
    const Project project = m_project;
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, generation, path, fromVideo]()
    {
        const Loaded result = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (generation == m_dataGeneration)
            {
                m_fileDataComing = false;
                dataLoaded(result.raw, result.worked, result.session, path, fromVideo, result.error, result.waiting);
            }
    });
    watcher->setFuture(QtConcurrent::run([path, project]()
    {
        Loaded result;
        const TelemetrySessionPtr main = TelemetryImporter::loadRecorded(path, &result.error);
        if (!main)
            {
                return result;
            }
        QVector<DataSource> sources = project.moreData;
        const QString error = result.error;
        result = workOut(DataSources::loadAndMerge(main, &sources), project);
        result.error = error;
        return result;
    }));
}

void SimpleController::dataLoaded(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &worked,
                                  const TelemetrySessionPtr &session, const QString &path, bool fromVideo, const QString &error, bool waiting)
{
    if (!session)
        {
            // a video without data of its own is no error: the data is asked for (over the demo, not yet)
            setMessage(fromVideo && m_demo ? QString()
                       : fromVideo ? tr("This video has no data of its own: open the file of the ride.")
                       : (error.isEmpty() ? tr("%1 could not be read.").arg(QFileInfo(path).fileName()) : error));
            return;
        }
    m_raw = raw;
    m_worked = worked;
    m_session = session;
    m_demo = false;
    m_project.telemetryPath = path;
    if (!fromVideo)
        {
            AppSettings::addRecentFile(AppSettings::RecentData, path);
            emit recentChanged();
        }
    m_arrivalTries = 0;
    if (waiting)
        {
            m_arrivals.start();
        }
    useBackground();
    emit dataChanged();
    emit lapsChanged();
    emit projectChanged();
    setMessage(QString());
    // the first gauges, unless the project came with its own
    firstGauges();
    rebuildOverlay();
    followClocks();
}

void SimpleController::chooseTemplate(int index)
{
    if (index >= 0 && index < TemplateLibrary::entries().size())
        {
            byHand();
            applyTemplate(index);
        }
}

void SimpleController::applyTemplate(int index)
{
    const QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
    if (index < 0 || index >= entries.size())
        {
            return;
        }
    m_templateIndex = index;
    const GaugeTemplate chosen = TemplateLibrary::get(entries.at(index).id);
    m_project.overlay = chosen.overlayItems(aspect());
    // without a video the dash rides over the route in 3D
    if (m_project.videoPath.isEmpty())
        {
            m_project.overlay = flyoverItems() + m_project.overlay;
        }
    m_project.theme = chosen.theme;
    m_project.layoutAspect = aspect();
    rebuildOverlay();
    if (m_session && m_probed)
        {
            followClocks();
        }
}

void SimpleController::rebuildOverlay()
{
    if (m_project.layoutAspect > 0.0 && std::abs(m_project.layoutAspect - aspect()) > 0.01)
        {
            // laid out before the video's shape was known: again from their nearest edges
            const Overlay placed(m_project.overlay, nullptr, AppSettings::units(), m_project.theme);
            m_project.overlay = GaugeTemplate::fromOverlay(placed, m_project.theme, m_project.layoutAspect).overlayItems(aspect());
            m_project.layoutAspect = aspect();
        }
    m_overlay = std::make_shared<const Overlay>(m_project.overlay, m_session, AppSettings::units(), m_project.theme);
    emit overlayChanged();
}

void SimpleController::setSync(double offset, const QString &text)
{
    m_project.syncOffset = offset;
    m_syncText = text;
    emit syncChanged();
    emit overlayChanged();
}

void SimpleController::followClocks()
{
    if (!m_project.syncIsManual)
        {
            syncByClocks();
        }
}

void SimpleController::syncByClocks()
{
    if (!m_session || m_project.videoPath.isEmpty() || !m_probed)
        {
            return;
        }
    m_project.syncIsManual = false;
    // the made-up ride has no clock of the camera's to meet
    if (m_demo)
        {
            setSync(0.0, tr("The demo ride: open the data of your own to put it in step."));
            return;
        }
    // data read out of the video itself is on the video's clock already
    if (QFileInfo(m_project.videoPath) == QFileInfo(m_project.telemetryPath))
        {
            setSync(0.0, tr("The data comes from this video."));
            return;
        }
    const QDateTime wallClock = VideoSync::wallClockFromFileName(QFileInfo(m_project.videoPath).fileName());
    const int systemUtcOffset = wallClock.isValid() ? QDateTime(wallClock.date(), wallClock.time()).offsetFromUtc() : 0;
    const VideoSync::Result result = VideoSync::match(*m_session, wallClock, m_info.duration * m_project.sync().rate, systemUtcOffset);
    if (result.ok)
        {
            setSync(result.offsetSeconds, tr("In step by the clocks."));
        }
    else
        {
            setSync(0.0, tr("Not in step: move the data until the gauges match the picture."));
        }
}

void SimpleController::nudgeSync(double seconds)
{
    m_project.syncIsManual = true;
    setModified();
    setSync(m_project.syncOffset + seconds, tr("Moved by hand."));
}

void SimpleController::setSyncOffset(double seconds)
{
    if (std::isfinite(seconds))
        {
            m_project.syncIsManual = true;
            setModified();
            setSync(seconds, tr("Moved by hand."));
        }
}

int SimpleController::gaugeAt(double x, double y, double width, double height) const
{
    return m_overlay->itemAt(QPointF(x, y), pictureFrame(width, height));
}

QRectF SimpleController::gaugeRect(int index, double width, double height) const
{
    return index >= 0 && index < m_overlay->count() ? m_overlay->itemRect(index, pictureFrame(width, height)) : QRectF();
}

void SimpleController::moveGauge(int index, double x, double y, bool done)
{
    if (index < 0 || index >= m_project.overlay.size() || index >= m_overlay->count())
        {
            return;
        }
    OverlayItem &item = m_project.overlay[index];
    // the whole of it on the picture
    const QRectF unit = m_overlay->itemRect(index, pictureFrame(1000.0, 1000.0 / aspect()));
    const double width = unit.width() / 1000.0;
    item.x = onPicture(x, width);
    item.y = onPicture(y, item.height);
    if (done)
        {
            byHand();
            rebuildOverlay();
            return;
        }
    // only what is drawn follows the finger: nothing bound again
    m_overlay = std::make_shared<const Overlay>(m_overlay->withGeometry(index, item.x, item.y, item.height, item.aspect));
    emit overlayChanged();
}

void SimpleController::resizeGauge(int index, double height)
{
    changeGauge(index, [height](OverlayItem &item)
    {
        const double wanted = std::clamp(height, 0.05, kMaxGaugeHeight);
        // about its middle
        item.y += (item.height - wanted) / 2.0;
        item.height = wanted;
        item.y = onPicture(item.y, item.height);
    });
}

void SimpleController::addGauge(const QString &gaugeId)
{
    const Gauge gauge = GaugeLibrary::gauge(gaugeId);
    if (gauge.isEmpty())
        {
            return;
        }
    OverlayItem item;
    item.gaugeId = gaugeId;
    item.channel = GaugeLibrary::defaultChannel(gauge, m_session ? m_session->channelIds() : QStringList());
    // near the middle, as the full window places one: a quarter of the picture's height, a wide
    // one no wider than about half of it, each new one a little further down right
    const double step = 0.03 * (m_project.overlay.size() % 8);
    const double shape = gauge.size.width() / gauge.size.height();
    item.height = std::min(0.25, 0.45 * aspect() / shape);
    const double width = item.height * shape / aspect();
    item.x = std::min(0.35 + step, 1.0 - width);
    item.y = std::min(0.3 + step, 1.0 - item.height);
    m_project.overlay.append(item);
    byHand();
    rebuildOverlay();
}

void SimpleController::removeGauge(int index)
{
    if (index >= 0 && index < m_project.overlay.size())
        {
            m_project.overlay.removeAt(index);
            byHand();
            rebuildOverlay();
        }
}

void SimpleController::changeGauge(int index, const std::function<void(OverlayItem &)> &change)
{
    if (index >= 0 && index < m_project.overlay.size())
        {
            change(m_project.overlay[index]);
            byHand();
            rebuildOverlay();
        }
}

void SimpleController::byHand()
{
    setModified();
}

void SimpleController::setGaugeRect(int index, double x, double y, double width, double height, bool done)
{
    if (index < 0 || index >= m_project.overlay.size() || index >= m_overlay->count() || !(width > 0.0) || !(height > 0.0))
        {
            return;
        }
    OverlayItem &item = m_project.overlay[index];
    item.height = std::clamp(height, 0.03, kMaxGaugeHeight);
    if (item.aspect > 0.0)
        {
            // set free: the width its own
            item.aspect = std::clamp(width, 0.02, kMaxGaugeHeight) * aspect() / item.height;
        }
    const QRectF unit = m_overlay->withGeometry(index, item.x, item.y, item.height, item.aspect)
                        .itemRect(index, pictureFrame(1.0, 1.0 / aspect()));
    item.x = onPicture(x, unit.width());
    item.y = onPicture(y, item.height);
    if (done)
        {
            byHand();
            rebuildOverlay();
            return;
        }
    m_overlay = std::make_shared<const Overlay>(m_overlay->withGeometry(index, item.x, item.y, item.height, item.aspect));
    emit overlayChanged();
}

QVariantMap SimpleController::gaugeProperties(int index) const
{
    QVariantMap properties;
    if (index < 0 || index >= m_project.overlay.size())
        {
            return properties;
        }
    const OverlayItem &item = m_project.overlay.at(index);
    const Units units = AppSettings::units();
    QString name = item.gaugeId;
    for (const GaugeLibrary::Entry &entry : GaugeLibrary::entries())
        {
            name = entry.id == item.gaugeId ? entry.name : name;
        }
    auto display = [this](const QString &channel, const Units &shownIn)
    {
        const Channel *data = m_session ? m_session->channel(channel) : nullptr;
        return ChannelFormat::displayFor(channel, data ? data->unit() : QString(), shownIn);
    };
    // what the data has, and what it is bound to even when the data lacks it
    QStringList ids = m_session ? m_session->channelIds() : QStringList();
    if (!item.channel.isEmpty() && !ids.contains(item.channel))
        {
            ids.prepend(item.channel);
        }
    QStringList names;
    for (const QString &id : ids)
        {
            names << display(id, units).name;
        }
    properties["name"] = name;
    properties["channel"] = item.channel;
    properties["channelIds"] = ids;
    properties["channelNames"] = names;
    properties["height"] = item.height;
    properties["textScale"] = item.textScale;
    properties["opacity"] = item.opacity;
    properties["hasScale"] = GaugeLibrary::hasScale(GaugeLibrary::gauge(item.gaugeId));
    properties["scaleMax"] = item.scaleMax;
    properties["unit"] = display(item.channel, item.unitsFor(units)).unit;
    properties["units"] = int(item.units);
    properties["ownColors"] = item.ownTheme;
    QVariantList colors;
    for (QColor GaugeTheme::*role : kColorRoles)
        {
            colors << (item.ownTheme ? item.theme : m_project.theme).*role;
        }
    properties["colors"] = colors;
    properties["keepShape"] = item.aspect <= 0.0;
    if (index >= m_overlay->count())
        {
            return properties;
        }
    const Overlay &overlay = *m_overlay;
    properties["width"] = overlay.itemRect(index, pictureFrame(1.0, 1.0 / aspect())).width();
    properties["hasGraph"] = overlay.hasGraph(index);
    properties["byDistance"] = overlay.byDistance(index);
    properties["hasRoute"] = overlay.hasRoute(index);
    properties["streets"] = overlay.hasStreets(index);
    QStringList mapIds{QString()};
    QStringList mapNames{tr("As in the settings")};
    for (const TileStore::Provider &provider : AppSettings::usableMapProviders())
        {
            mapIds << provider.id;
            mapNames << provider.name;
        }
    properties["mapIds"] = mapIds;
    properties["mapNames"] = mapNames;
    properties["map"] = item.map;
    // the close-up, and by speed what it was, else a ride's: close in town, wide on the open road
    const Units shownIn = item.unitsFor(units);
    const GaugeComponent::SpeedZoom zoom = item.speedZoom.isOn() ? item.speedZoom
                                           : GaugeComponent::SpeedZoom{1500.0, 10.0 / 3.6, 40.0 / 3.6};
    properties["closeUp"] = overlay.closeUp(index);
    properties["bySpeed"] = item.speedZoom.isOn();
    properties["far"] = zoom.far;
    properties["slow"] = std::round(zoom.slow * shownIn.speedScale());
    properties["fast"] = std::round(zoom.fast * shownIn.speedScale());
    properties["speedUnit"] = shownIn.speedLabel();
    properties["canBySpeed"] = m_session && m_session->channel(ChannelId::Speed);
    properties["headingUp"] = overlay.headingUp(index);
    properties["flyover"] = overlay.flyover(index);
    properties["ghost"] = overlay.ghost(index);
    const GaugeComponent::FlyoverCamera camera = overlay.flyoverCamera(index);
    properties["tilt"] = camera.tilt;
    properties["cameraHeight"] = camera.height;
    properties["hills"] = camera.hills;
    return properties;
}

void SimpleController::setGaugeChannel(int index, const QString &channel)
{
    changeGauge(index, [channel](OverlayItem &item)
    {
        if (item.channel != channel)
            {
                // an end chosen for another measurement means nothing for this one
                item.scaleMax = 0.0;
            }
        item.channel = channel;
    });
}

void SimpleController::setGaugeTextScale(int index, double scale)
{
    changeGauge(index, [scale](OverlayItem &item) { item.textScale = std::clamp(scale, 0.5, 3.0); });
}

void SimpleController::setGaugeOpacity(int index, double opacity)
{
    changeGauge(index, [opacity](OverlayItem &item) { item.opacity = std::clamp(opacity, 0.1, 1.0); });
}

void SimpleController::setGaugeScaleMax(int index, double max)
{
    changeGauge(index, [max](OverlayItem &item) { item.scaleMax = std::max(0.0, max); });
}

void SimpleController::setGaugeUnits(int index, int units)
{
    if (units >= OverlayItem::AsSettings && units <= OverlayItem::ImperialUnits)
        {
            changeGauge(index, [units](OverlayItem &item) { item.units = OverlayItem::UnitSystem(units); });
        }
}

void SimpleController::setGaugeOwnColors(int index, bool own)
{
    const GaugeTheme common = m_project.theme;
    changeGauge(index, [own, common](OverlayItem &item)
    {
        // starting from the look of all gauges
        item.ownTheme = own;
        item.theme = own ? common : GaugeTheme();
    });
}

void SimpleController::setGaugeColor(int index, int role, const QColor &color)
{
    if (role < 0 || role >= int(std::size(kColorRoles)) || !color.isValid()
            || index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    if (m_project.overlay.at(index).ownTheme)
        {
            changeGauge(index, [role, color](OverlayItem &item) { item.theme.*kColorRoles[role] = color; });
            return;
        }
    // all gauges
    m_project.theme.*kColorRoles[role] = color;
    byHand();
    rebuildOverlay();
}

void SimpleController::setGaugeKeepShape(int index, bool keep)
{
    if (index >= m_overlay->count())
        {
            return;
        }
    // freed as it is, so that nothing moves until its width is changed
    const QRectF rect = m_overlay->itemRect(index, pictureFrame(1000.0, 1000.0));
    changeGauge(index, [keep, rect](OverlayItem &item) { item.aspect = keep ? 0.0 : rect.width() / rect.height(); });
}

void SimpleController::setGaugeWidth(int index, double width)
{
    const double pictureAspect = aspect();
    changeGauge(index, [width, pictureAspect](OverlayItem &item)
    {
        const double wanted = std::clamp(width, 0.02, std::max(1.0 - item.x, 0.02));
        item.aspect = wanted * pictureAspect / item.height;
    });
}

void SimpleController::setGaugeByDistance(int index, bool on)
{
    changeGauge(index, [on](OverlayItem &item) { item.byDistance = on ? 1 : 0; });
}

void SimpleController::setGaugeStreets(int index, bool shown)
{
    changeGauge(index, [shown](OverlayItem &item) { item.streets = shown ? OverlayItem::Shown : OverlayItem::Hidden; });
}

void SimpleController::setGaugeMap(int index, const QString &map)
{
    changeGauge(index, [map](OverlayItem &item)
    {
        item.map = map;
        // a map chosen is a map shown
        if (!map.isEmpty())
            {
                item.streets = OverlayItem::Shown;
            }
    });
}

bool SimpleController::setGaugeCloseUp(int index, double meters, double far, double slow, double fast)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return false;
        }
    const Units units = m_project.overlay.at(index).unitsFor(AppSettings::units());
    GaugeComponent::SpeedZoom zoom;
    if (far > 0.0)
        {
            zoom = {far, slow / units.speedScale(), fast / units.speedScale()};
            if (!zoom.isOn())
                {
                    return false;
                }
        }
    changeGauge(index, [meters, zoom](OverlayItem &item)
    {
        item.closeUp = std::max(0.0, meters);
        item.speedZoom = item.closeUp > 0.0 ? zoom : GaugeComponent::SpeedZoom();
    });
    return true;
}

void SimpleController::setGaugeHeadingUp(int index, bool up)
{
    changeGauge(index, [up](OverlayItem &item) { item.headingUp = up ? 1 : 0; });
}

void SimpleController::setGaugeFlyover(int index, bool on)
{
    changeGauge(index, [on](OverlayItem &item) { item.flyover = on ? 1 : 0; });
}

void SimpleController::setGaugeGhost(int index, bool on)
{
    changeGauge(index, [on](OverlayItem &item) { item.ghost = on ? 1 : 0; });
}

void SimpleController::setGaugeFlyoverCamera(int index, double tilt, double height, double hills)
{
    const GaugeComponent::FlyoverCamera camera = tilt > 0.0
            ? GaugeComponent::FlyoverCamera{std::clamp(tilt, 10.0, 85.0), std::clamp(height, 10.0, 20000.0), std::clamp(hills, 1.0, 5.0)}
            : GaugeComponent::FlyoverCamera{0.0, 0.0, 0.0};
    changeGauge(index, [camera](OverlayItem &item) { item.flyoverCamera = camera; });
}

void SimpleController::addMusic(const QUrl &url)
{
    const QString path = localPath(url);
    if (path.isEmpty())
        {
            return;
        }
    // its length, for the next track to follow it; until known it plays to its end
    m_project.soundtrack.tracks.append({path});
    setModified();
    emit musicChanged();
    auto *watcher = new QFutureWatcher<double>(this);
    connect(watcher, &QFutureWatcher<double>::finished, this, [this, watcher, path]()
    {
        const double duration = watcher->result();
        watcher->deleteLater();
        for (MusicTrack &track : m_project.soundtrack.tracks)
            {
                if (track.path == path && track.duration <= 0.0)
                    {
                        track.duration = duration;
                    }
            }
        emit musicChanged();
    });
    const QString ffprobe = FfmpegTools::ffprobeProgram();
    const bool preferOwn = AppSettings::ownVideoEngine();
    watcher->setFuture(QtConcurrent::run([ffprobe, path, preferOwn]()
    {
        return SoundtrackMix::probeDuration(ffprobe, path, preferOwn);
    }));
}

void SimpleController::removeMusic(int index)
{
    if (index >= 0 && index < m_project.soundtrack.tracks.size())
        {
            m_project.soundtrack.tracks.removeAt(index);
            setModified();
            emit musicChanged();
        }
}

QVariantMap SimpleController::musicAt(qint64 videoMs) const
{
    const Soundtrack &soundtrack = m_project.soundtrack;
    const Soundtrack::Moment moment = soundtrack.hasMusic() ? soundtrack.at(soundtrack.musicTime(videoMs / 1000.0, m_project.cuts))
                                      : Soundtrack::Moment();
    QVariantMap music;
    music["url"] = moment.track >= 0 ? QUrl::fromLocalFile(soundtrack.tracks.at(moment.track).path) : QUrl();
    music["position"] = qint64(std::llround(moment.position * 1000.0));
    // a player plays no louder than the file: a boost is heard in the export only
    music["volume"] = std::min(1.0, moment.gain * soundtrack.musicGain());
    return music;
}

void SimpleController::startExport()
{
    if (m_thread || m_exportWaitsForEncoders)
        {
            return;
        }
    m_exportStart = 0.0;
    m_exportLength = 0.0;
    m_exportName.clear();
    runExport();
}

void SimpleController::startExportRange(double from, double to)
{
    if (m_thread || m_exportWaitsForEncoders || !std::isfinite(from) || !std::isfinite(to))
        {
            return;
        }
    const double start = std::clamp(from, 0.0, std::max(0.0, m_info.duration));
    const double end = std::clamp(to, 0.0, std::max(0.0, m_info.duration));
    if (end <= start)
        {
            m_exportText = tr("The end has to be after the start.");
            emit exportChanged();
            return;
        }
    m_exportStart = start;
    m_exportLength = end - start;
    m_exportName.clear();
    runExport();
}

void SimpleController::exportLap(int number)
{
    if (m_thread || m_exportWaitsForEncoders || !m_session || number < 1 || number > m_session->laps().size())
        {
            return;
        }
    const TelemetrySession::Lap &lap = m_session->laps().at(number - 1);
    const SyncMap sync = m_project.sync();
    const double start = std::max(0.0, sync.videoTime(lap.startTime));
    const double end = std::min(m_info.duration, sync.videoTime(lap.endTime));
    if (end <= start)
        {
            m_exportText = tr("This lap is not in the video.");
            emit exportChanged();
            return;
        }
    m_exportStart = start;
    m_exportLength = end - start;
    m_exportName = tr("lap%1").arg(number);
    runExport();
}

void SimpleController::runExport()
{
    if (m_thread || !m_session || !m_probed)
        {
            return;
        }
    if (!m_encodersFound)
        {
            // which encoders work here takes a few test encodes, once
            m_exportWaitsForEncoders = true;
            m_exportText = tr("Finding the encoders this device can use...");
            emit exportChanged();
            auto *watcher = new QFutureWatcher<VideoEncoders::ForEngine>(this);
            connect(watcher, &QFutureWatcher<VideoEncoders::ForEngine>::finished, this, [this, watcher]()
            {
                m_encoders = watcher->result();
                watcher->deleteLater();
                m_encodersFound = true;
                if (m_exportWaitsForEncoders)
                    {
                        m_exportWaitsForEncoders = false;
                        runExport();
                    }
            });
            const bool preferOwn = AppSettings::ownVideoEngine();
            const QString ffmpeg = FfmpegTools::ffmpegProgram();
            watcher->setFuture(QtConcurrent::run([preferOwn, ffmpeg]()
            {
                return VideoEncoders::detectFor(preferOwn, ffmpeg);
            }));
            return;
        }
    if (m_encoders.encoders.isEmpty())
        {
            m_exportText = tr("No encoder works on this device.");
            emit exportChanged();
            return;
        }
    const QVector<ExportProfile> profiles = allProfiles();
    const ExportProfile &profile = profiles.at(std::clamp(m_profileIndex, 0, int(profiles.size()) - 1));
    ExportSettings settings = ExportProfiles::apply(profile, AppSettings::exportDefaults(), m_info, m_encoders.encoders);
    QString suffix = profile.suffix;
    if (ExportSettings::needsMatroska(settings.audio))
        {
            suffix = QStringLiteral("mkv");
        }
    settings.start = m_exportStart;
    settings.duration = m_exportLength;
    if (m_gaugesOnly)
        {
            // laid over the video elsewhere: VP9 with its see-through, no sound
            settings.picture = ExportSettings::GaugesOnly;
            settings.audio = ExportSettings::NoAudio;
            suffix = QStringLiteral("webm");
        }
    // without a video: the gauges over the background's color
    settings.background = m_project.background.color;
    // beside the video (or the data), or in the folder of the settings; never over a file that is there
    const QFileInfo source(!m_project.videoPath.isEmpty() ? m_project.videoPath : m_project.telemetryPath);
    QString folder = !AppSettings::exportFolder().isEmpty() ? AppSettings::exportFolder()
                     : m_demo ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : source.absolutePath();
    if (folder.isEmpty() || !QDir(folder).exists())
        {
            folder = QDir::homePath();
        }
    const QString base = (m_demo ? QStringLiteral("demo") : source.completeBaseName()) + (m_gaugesOnly ? "_gauges" : "_overlay")
                         + (m_exportName.isEmpty() ? QString() : "_" + m_exportName);
    QString path = QDir(folder).filePath(base + "." + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        {
            path = QDir(folder).filePath(QString("%1_%2.%3").arg(base).arg(n).arg(suffix));
        }
    settings.outputPath = path;
    m_runningPath = path;
    m_exportedPath.clear();

    const ExportPlan plan = ExportPlan::make(m_project.videoPath, m_info, settings, *m_overlay, m_project.cuts, m_project.soundtrack);
    auto *job = new ExportJob(FfmpegTools::ffmpegProgram(), plan, *m_overlay, m_project.sync(), nullptr,
                              m_encoders.own ? ExportJob::OwnEngine : ExportJob::ProgramEngine);
    m_job = job;
    m_thread = new QThread(this);
    job->moveToThread(m_thread);
    connect(m_thread, &QThread::started, job, &ExportJob::start);
    connect(m_thread, &QThread::finished, job, &QObject::deleteLater);
    connect(job, &ExportJob::progress, this, [this](qint64 frame, qint64 total, double)
    {
        m_exportProgress = total > 0 ? double(frame) / total : 0.0;
        m_exportText = tr("Exporting... %1 %").arg(int(m_exportProgress * 100.0));
        emit exportChanged();
    });
    connect(job, &ExportJob::finished, this, [this](ExportJob::Outcome outcome, const QString &message)
    {
        exportFinished(int(outcome), message);
    });
    m_exportProgress = 0.0;
    m_exportText = tr("Starting...");
    m_thread->start();
    emit exportChanged();
}

void SimpleController::stopExport()
{
    if (m_exportWaitsForEncoders)
        {
            m_exportWaitsForEncoders = false;
            m_exportText.clear();
            emit exportChanged();
            return;
        }
    if (m_job)
        {
            // what is done so far stays a playable video; the job lives on its own thread
            QMetaObject::invokeMethod(m_job, &ExportJob::stop, Qt::QueuedConnection);
        }
}

void SimpleController::exportFinished(int outcome, const QString &message)
{
    m_thread->quit();
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
    switch (ExportJob::Outcome(outcome))
        {
        case ExportJob::Succeeded:
            m_exportedPath = m_runningPath;
            m_exportText = tr("Saved %1").arg(QFileInfo(m_runningPath).fileName());
            break;
        case ExportJob::Stopped:
            m_exportedPath = m_runningPath;
            m_exportText = tr("Stopped: what was done is in %1").arg(QFileInfo(m_runningPath).fileName());
            break;
        case ExportJob::Canceled:
            m_exportText = tr("Canceled.");
            break;
        case ExportJob::Failed:
            m_exportText = message.isEmpty() ? tr("The export failed.") : message;
            break;
        }
    m_exportProgress = 0.0;
    emit exportChanged();
}

void SimpleController::goBack()
{
    if (m_canGoBack)
        {
            emit backRequested();
        }
}
