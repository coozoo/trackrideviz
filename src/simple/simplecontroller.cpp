#include "simplecontroller.h"

#include "gauge/gaugelibrary.h"
#include "laps/lapengine.h"
#include "maps/tilestore.h"
#include "overlay/gaugetemplate.h"
#include "overlay/templatelibrary.h"
#include "project/projectdata.h"
#include "settings/appsettings.h"
#include "sync/videosync.h"
#include "telemetry/datasources.h"
#include "telemetry/telemetryimporter.h"
#include "units/units.h"
#include "video/exportjob.h"
#include "video/exportplan.h"
#include "video/exportprofile.h"
#include "video/ffmpegtools.h"
#include "video/libav.h"
#include "weather/weather.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>

namespace
{
// the template a first video gets: the shipped one for any ride
const char kFirstTemplate[] = "Ride";
// how long the heights of the terrain and the weather are waited for, a look every two seconds
const int kArrivalLookMs = 2000;
const int kArrivalTries = 30;

struct Loaded
{
    TelemetrySessionPtr raw;
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
    result.session = worked ? LapEngine::apply(worked, project.startFinish, project.sectors, project.referenceLap) : worked;
    return result;
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
    return m_project.videoPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_project.videoPath);
}

QString SimpleController::videoName() const
{
    return QFileInfo(m_project.videoPath).fileName();
}

QString SimpleController::dataName() const
{
    return m_session ? QFileInfo(m_project.telemetryPath).fileName() : QString();
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
            emit musicChanged();
        }
}

void SimpleController::setMusicVolume(int percent)
{
    percent = std::clamp(percent, 0, Soundtrack::kMaxVolume);
    if (percent != m_project.soundtrack.musicVolume)
        {
            m_project.soundtrack.musicVolume = percent;
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
    m_project.videoShifts.clear();
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
        emit videoChanged();
        setMessage(QString());
        if (m_session || m_fileDataComing)
            {
                // the gauges laid out for its shape, and in step by the clocks (or once the data is read)
                rebuildOverlay();
                syncByClocks();
            }
        else
            {
                // a camera that records its own data (GoPro): read out of the video
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
            loadData(path, false);
        }
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
                dataLoaded(result.raw, result.session, path, fromVideo, result.error, result.waiting);
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

void SimpleController::dataLoaded(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session, const QString &path,
                                  bool fromVideo, const QString &error, bool waiting)
{
    if (!session)
        {
            // a video without data of its own is no error: the data is asked for
            setMessage(fromVideo ? tr("This video has no data of its own: open the file of the ride.")
                       : (error.isEmpty() ? tr("%1 could not be read.").arg(QFileInfo(path).fileName()) : error));
            return;
        }
    m_raw = raw;
    m_session = session;
    m_project.telemetryPath = path;
    m_arrivalTries = 0;
    if (waiting)
        {
            m_arrivals.start();
        }
    emit dataChanged();
    setMessage(QString());
    if (m_templateIndex < 0)
        {
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
                    chooseTemplate(first);
                    return;
                }
        }
    rebuildOverlay();
    syncByClocks();
}

void SimpleController::chooseTemplate(int index)
{
    const QVector<TemplateLibrary::Entry> entries = TemplateLibrary::entries();
    if (index < 0 || index >= entries.size())
        {
            return;
        }
    m_templateIndex = index;
    const GaugeTemplate chosen = TemplateLibrary::get(entries.at(index).id);
    m_project.overlay = chosen.overlayItems(aspect());
    m_project.theme = chosen.theme;
    m_project.layoutAspect = aspect();
    rebuildOverlay();
    if (m_session && m_probed)
        {
            syncByClocks();
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

void SimpleController::syncByClocks()
{
    if (!m_session || m_project.videoPath.isEmpty() || !m_probed)
        {
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
    setSync(m_project.syncOffset + seconds, tr("Moved by hand."));
}

void SimpleController::setSyncOffset(double seconds)
{
    if (std::isfinite(seconds))
        {
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
    item.x = std::clamp(x, 0.0, std::max(0.0, 1.0 - width));
    item.y = std::clamp(y, 0.0, std::max(0.0, 1.0 - item.height));
    if (done)
        {
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
        const double wanted = std::clamp(height, 0.05, 1.0);
        // about its middle
        item.y += (item.height - wanted) / 2.0;
        item.height = wanted;
        item.y = std::clamp(item.y, 0.0, std::max(0.0, 1.0 - item.height));
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
    rebuildOverlay();
}

void SimpleController::removeGauge(int index)
{
    if (index >= 0 && index < m_project.overlay.size())
        {
            m_project.overlay.removeAt(index);
            rebuildOverlay();
        }
}

void SimpleController::changeGauge(int index, const std::function<void(OverlayItem &)> &change)
{
    if (index >= 0 && index < m_project.overlay.size())
        {
            change(m_project.overlay[index]);
            rebuildOverlay();
        }
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

void SimpleController::addMusic(const QUrl &url)
{
    const QString path = localPath(url);
    if (path.isEmpty())
        {
            return;
        }
    // its length, for the next track to follow it; until known it plays to its end
    m_project.soundtrack.tracks.append({path});
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
                        startExport();
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
    // beside the video, or in the folder of the settings; never over a file that is there
    const QFileInfo video(m_project.videoPath);
    const QString folder = !AppSettings::exportFolder().isEmpty() ? AppSettings::exportFolder() : video.absolutePath();
    QString path = QDir(folder).filePath(video.completeBaseName() + "_overlay." + suffix);
    for (int n = 2; QFileInfo::exists(path); ++n)
        {
            path = QDir(folder).filePath(QString("%1_overlay_%2.%3").arg(video.completeBaseName()).arg(n).arg(suffix));
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
