#include "mainwindow.h"

#include "backgrounddialog.h"
#include "playback.h"

#include "backgrounddialog.h"
#include "datapanel.h"
#include "datasourcesdialog.h"
#include "diagnostics.h"
#include "dockhome.h"
#include "icons.h"
#include "joindialog.h"
#include "video/lapexport.h"
#include "video/videojoin.h"
#include "lapcomparepanel.h"
#include "lapspanel.h"
#include "musicpanel.h"
#include "playback.h"
#include "timelinepanel.h"

#include "appearance.h"
#include "exportdialog.h"
#include "gauge/gaugefonts.h"
#include "gauge/gaugelibrary.h"
#include "gaugelibrarypanel.h"
#include "maps/terrain.h"
#include "maps/tilestore.h"
#include "overlay/gaugetemplate.h"
#include "overlay/librarypackage.h"
#include "overlay/templatelibrary.h"
#include "project/projectdata.h"
#include "settings/appsettings.h"
#include "settingsdialog.h"
#include "sync/videosync.h"
#include "telemetry/dataedits.h"
#include "telemetry/derivedchannels.h"
#include "telemetry/gears.h"
#include "weather/weather.h"
#include "telemetry/telemetryimporter.h"
#include "telemetry/telemetrywriter.h"
#include "updater.h"
#include "videocanvas.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QCompleter>
#include <QFontDatabase>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QScreen>
#include <QAudioDevice>
#include <QDesktopServices>
#include <QDropEvent>
#include <QMetaEnum>
#include <QMimeData>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QVideoSink>
#include <QtConcurrent>

namespace
{
// The name of a Qt enum value, for the log: "LoadedMedia".
template <typename E>
QString enumKey(E value)
{
    const char *key = QMetaEnum::fromType<E>().valueToKey(int(value));
    return key ? QString::fromLatin1(key) : QString::number(int(value));
}

// The arrangement of the docks a saved layout is for: an older one is not restored.
// 2: Music beside Properties and Laps.
const int kLayoutVersion = 2;
const char kRepository[] = "https://github.com/coozoo/trackrideviz";
const char kSupportUkraine[] = "https://github.com/coozoo/trackrideviz#support-ukraine";
const char kManual[] = "https://github.com/coozoo/trackrideviz/blob/main/docs/manual.md";

// Selects all of a line edit's text when it is clicked or tabbed into.
class SelectAllOnFocus : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FocusIn)
            {
                // after the click that gave the focus has placed the cursor
                QLineEdit *edit = qobject_cast<QLineEdit *>(watched);
                QTimer::singleShot(0, edit, [edit]() { edit->selectAll(); });
            }
        return false;
    }
};

struct ImportResult
{
    TelemetrySessionPtr main;      // the main file as it gave it
    TelemetrySessionPtr raw;       // with the channels of the added files
    TelemetrySessionPtr session;   // corrected, and completed with what could be worked out
    QVector<DataSource> sources;   // the added files, their shifts following the clocks
    bool waiting{false};           // for heights of the terrain still being fetched
    bool weatherWaiting{false};    // for the weather of the ride
    QString error;
};

// One change of the overlay. Whole states are kept rather than differences:
// they are a handful of numbers, and "set it to exactly this" cannot go wrong.
class OverlayCommand : public QUndoCommand
{
public:
    OverlayCommand(const QString &text, std::function<void(bool)> apply)
        : QUndoCommand(text), m_apply(std::move(apply))
    {
    }
    void undo() override { m_apply(false); }
    void redo() override { m_apply(true); }

private:
    std::function<void(bool)> m_apply; // true: the new state, false: the old one
};

struct ThemeRole
{
    const char *label;
    const char *tip;
    QColor GaugeTheme::*color;
};

enum ChannelColumn { NameColumn, NowColumn, UnitColumn, MinColumn, MaxColumn };

QString formatTime(qint64 ms)
{
    const qint64 seconds = ms / 1000;
    const QString minSec = QString("%1:%2")
                           .arg(seconds / 60 % 60, 2, 10, QChar('0'))
                           .arg(seconds % 60, 2, 10, QChar('0'));
    return seconds >= 3600 ? QString("%1:%2").arg(seconds / 3600).arg(minSec) : minSec;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // the first time, before a layout is saved: as much as a laptop has, never past the screen
    resize(QSize(1280, 800).boundedTo(screen()->availableGeometry().size() * 0.92));

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoSink(m_videoSink);
    // what plays: the video, or the data's own clock when there is a background instead
    m_playback = new Playback(m_player, this);
    m_musicPlayer = new QMediaPlayer(this);
    m_musicOutput = new QAudioOutput(this);
    m_musicPlayer->setAudioOutput(m_musicOutput);

    m_undoStack = new QUndoStack(this);
    setCentralWidget(createCentralWidget());
    createDocks();
    createMenus();
    statusBar();

    m_units = AppSettings::units();
    m_rider = AppSettings::powerRider();
    m_weather = AppSettings::weather();
    m_drivetrain = AppSettings::drivetrain();
    applyMapSettings();
    rebuildOverlay();
    // map tiles come one by one: gauges with a street map are drawn again with them
    connect(TileStore::instance(), &TileStore::arrived, this, [this]()
    {
        // only gauges with a street map draw tiles (and tiles also come for the Laps panel's map);
        // the gauges' properties stay as they are, so a choice being made there is not undone
        const Overlay &shown = m_canvas->overlay();
        for (int i = 0; i < shown.count(); ++i)
            {
                if (shown.hasStreets(i))
                    {
                        m_canvas->setOverlay(Overlay(m_project.overlay, m_session, m_units, m_project.theme));
                        return;
                    }
            }
    });
    // Heights of the terrain and the weather of the ride that were missing when the data was
    // worked out. They come in batches; the data is worked out again once they have stopped
    // coming, or every few seconds while they go on: not once for every batch.
    m_arrivalTimer = new QTimer(this);
    m_arrivalTimer->setSingleShot(true);
    m_arrivalTimer->setInterval(1000);
    connect(m_arrivalTimer, &QTimer::timeout, this, [this]()
    {
        if (!m_terrainWaiting && !m_weatherWaiting)
            {
                return;
            }
        const bool more = TileStore::terrain()->pending() > 0 || WeatherStore::instance()->pending() > 0;
        if (more && m_sinceRecompute.isValid() && m_sinceRecompute.elapsed() < 5000)
            {
                m_arrivalTimer->start();
                return;
            }
        recomputeData();
    });
    auto arrived = [this]()
    {
        if (m_terrainWaiting || m_weatherWaiting)
            {
                m_arrivalTimer->start();
            }
    };
    connect(TileStore::terrain(), &TileStore::arrived, this, arrived);
    connect(WeatherStore::instance(), &WeatherStore::arrived, this, arrived);

    connect(m_canvas, &VideoCanvas::itemGeometryEdited, this, &MainWindow::onItemGeometryEdited);
    connect(m_canvas, &VideoCanvas::itemDeleteRequested, this, &MainWindow::removeGauge);
    connect(m_canvas, &VideoCanvas::itemMenuRequested, this, &MainWindow::showItemMenu);
    connect(m_canvas, &VideoCanvas::selectedItemChanged, this, &MainWindow::updateProperties);
    connect(m_libraryPanel, &GaugeLibraryPanel::addRequested, this, &MainWindow::addGauge);
    connect(m_canvas, &VideoCanvas::gaugeDropped, this, &MainWindow::addGaugeAt);
    connect(m_canvas, &VideoCanvas::templateDropped, this, &MainWindow::applyTemplate);
    // files dropped anywhere on the window
    setAcceptDrops(true);

    // a newer release: a link in the status bar and an entry in Help, nothing that pops up
    m_releaseCheck = new ReleaseCheck(this);
    connect(m_releaseCheck, &ReleaseCheck::newer, this, [this](const ReleaseCheck::Release &release)
    {
        m_newRelease = release;
        const bool skipped = release.tag == AppSettings::skippedRelease();
        if (!skipped || m_updateAsked)
            {
                m_updateLabel->setText(QString("<a href=\"#\">%1</a>").arg(tr("Version %1 is available").arg(release.version)));
                m_updateLabel->show();
                m_updateAction->setText(tr("Update to version %1...").arg(release.version));
                m_updateAction->setVisible(true);
            }
        if (m_updateAsked)
            {
                showNewRelease();
            }
    });
    connect(m_releaseCheck, &ReleaseCheck::upToDate, this, [this]()
    {
        if (m_updateAsked)
            {
                QMessageBox::information(this, tr("Check for updates"),
                                         tr("This is the latest version, %1.").arg(qApp->applicationVersion()));
            }
    });
    connect(m_releaseCheck, &ReleaseCheck::failed, this, [this](const QString &why)
    {
        DiagnosticLog::note("updates", why);
        if (m_updateAsked)
            {
                QMessageBox::warning(this, tr("Check for updates"), tr("Could not find out about newer versions: %1").arg(why));
            }
    });
    m_updateLabel = new QLabel(this);
    m_updateLabel->setTextFormat(Qt::RichText);
    m_updateLabel->setToolTip(tr("What is new, and how to get it"));
    m_updateLabel->hide();
    connect(m_updateLabel, &QLabel::linkActivated, this, &MainWindow::showNewRelease);
    statusBar()->addPermanentWidget(m_updateLabel);
    // once a day, a while after the start: the window first
    const QDateTime lastCheck = AppSettings::lastUpdateCheck();
    if (!ReleaseCheck::currentTag().isEmpty() && AppSettings::checkForUpdates()
            && (!lastCheck.isValid() || lastCheck.secsTo(QDateTime::currentDateTime()) > 20 * 3600))
        {
            QTimer::singleShot(8000, this, [this]() { checkForUpdates(false); });
        }
    // told from inside the canvas's painting: the gauges are placed again after it
    connect(m_canvas, &VideoCanvas::pictureAspectChanged, this, &MainWindow::fitLayoutTo, Qt::QueuedConnection);
    connect(m_libraryPanel, &GaugeLibraryPanel::applyTemplateRequested, this, &MainWindow::applyTemplate);
    connect(m_libraryPanel, &GaugeLibraryPanel::libraryChanged, this, &MainWindow::rebuildOverlay);
    updateProperties();

    connect(m_videoSink, &QVideoSink::videoFrameChanged, m_canvas, &VideoCanvas::setFrame);
    connect(m_playback, &Playback::positionChanged, m_canvas, &VideoCanvas::setPosition);
    connect(m_playback, &Playback::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_playback, &Playback::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_playback, &Playback::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    // a track just opened is put at its place once it can be
    connect(m_musicPlayer, &QMediaPlayer::mediaStatusChanged, this, &MainWindow::syncMusic);
    // the players' changes, for the Diagnostics dock
    for (QMediaPlayer *player : {m_player, m_musicPlayer})
        {
            const QString area = player == m_player ? QStringLiteral("video") : QStringLiteral("music");
            connect(player, &QMediaPlayer::mediaStatusChanged, this, [area](QMediaPlayer::MediaStatus status)
            {
                DiagnosticLog::note(area, QString("media: %1").arg(enumKey(status)));
            });
            connect(player, &QMediaPlayer::playbackStateChanged, this, [area](QMediaPlayer::PlaybackState state)
            {
                DiagnosticLog::note(area, QString("playback: %1").arg(enumKey(state)));
            });
            connect(player, &QMediaPlayer::errorOccurred, this, [area](QMediaPlayer::Error error, const QString &message)
            {
                DiagnosticLog::note(area, QString("error %1: %2").arg(enumKey(error), message));
            });
            connect(player, &QMediaPlayer::sourceChanged, this, [area](const QUrl &source)
            {
                DiagnosticLog::note(area, QString("source: %1").arg(source.toLocalFile()));
            });
        }
    connect(m_musicPlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &message)
    {
        if (error != QMediaPlayer::NoError)
            {
                statusBar()->showMessage(tr("Music: %1").arg(message), 5000);
            }
    });

    QShortcut *playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(playShortcut, &QShortcut::activated, this, &MainWindow::togglePlayback);

    // a few times a second is plenty for numbers somebody reads
    QTimer *valuesTimer = new QTimer(this);
    valuesTimer->setInterval(200);
    connect(valuesTimer, &QTimer::timeout, this, &MainWindow::updateChannelValues);
    connect(valuesTimer, &QTimer::timeout, this, [this]()
    {
        if (m_dataDock->isVisible())
            {
                m_dataPanel->setTime(shownTelemetryTime());
            }
        if (m_lapsDock->isVisible())
            {
                m_lapsPanel->setTime(shownTelemetryTime());
            }
    });
    valuesTimer->start();

    updateSourcesInfo();
    updateTimeLabel();
    updateWindowTitle();

    // the docks as the program places them, for View > Reset layout
    m_defaultLayout = saveState(kLayoutVersion);
    if (AppSettings::saveWindowLayout())
        {
            QSettings settings;
            restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
            // a layout of an older version keeps the docks added since where they do not belong
            restoreState(settings.value("mainwindow/state").toByteArray(), kLayoutVersion);
        }
    // a floating dock's title double-clicked: back where it was docked, whatever it went through
    new DockHome(this);
    for (QDockWidget *dock : findChildren<QDockWidget *>())
        {
            connect(dock, &QDockWidget::topLevelChanged, this, [this, dock](bool floating)
            {
                // once Qt has placed it
                QMetaObject::invokeMethod(this, [this, dock, floating]()
                {
                    settleDock(dock, floating);
                }, Qt::QueuedConnection);
            });
        }
}

void MainWindow::settleDock(QDockWidget *dock, bool floating)
{
    if (!floating)
        {
            // back among tabs: in front, where it was looked for
            if (!tabifiedDockWidgets(dock).isEmpty())
                {
                    dock->raise();
                }
            return;
        }
    if (!dock->isFloating() || QApplication::mouseButtons() != Qt::NoButton)
        {
            // being dragged: where the mouse leaves it
            return;
        }
    // Floated by a double-click it opens right where it was docked, frameless, and reads as the
    // tabs' contents drawn over each other; on some desktops it lands off the screen and is lost
    // while the menu says it is shown. A little down and right of there, on a screen, in front.
    QRect frame = dock->frameGeometry();
    frame.translate(40, 40);
    const QScreen *screen = QGuiApplication::screenAt(frame.center());
    if (!screen)
        {
            screen = this->screen();
        }
    const QRect area = screen->availableGeometry();
    frame.setSize(frame.size().boundedTo(area.size() * 0.9));
    if (!area.contains(frame))
        {
            frame.moveCenter(geometry().center());
            frame.moveLeft(std::clamp(frame.left(), area.left(), area.right() - frame.width()));
            frame.moveTop(std::clamp(frame.top(), area.top(), area.bottom() - frame.height()));
        }
    dock->setGeometry(frame);
    dock->raise();
    dock->activateWindow();
}

namespace
{
QStringList localFiles(const QMimeData *data)
{
    QStringList paths;
    for (const QUrl &url : data->urls())
        {
            if (url.isLocalFile())
                {
                    paths << url.toLocalFile();
                }
        }
    return paths;
}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!localFiles(event->mimeData()).isEmpty())
        {
            event->acceptProposedAction();
        }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = localFiles(event->mimeData());
    if (paths.isEmpty())
        {
            return;
        }
    event->acceptProposedAction();
    // what to do is chosen after the drop is over: a question asked inside it would hold the
    // program the files came from
    QTimer::singleShot(0, this, [this, paths]() { openDropped(paths); });
}

void MainWindow::openDropped(const QStringList &paths)
{
    static const QStringList videos{"mp4", "mov", "mkv", "avi", "m4v", "mts", "webm"};
    QString video;
    QString data;
    QString project;
    QStringList library;
    QStringList unknown;
    for (const QString &path : paths)
        {
            const QFileInfo info(path);
            const QString suffix = info.suffix().toLower();
            if (suffix == QLatin1String(ProjectFile::Suffix))
                {
                    project = project.isEmpty() ? path : project;
                }
            else if (info.isDir() || suffix == LibraryPackage::GaugeSuffix || suffix == LibraryPackage::TemplateSuffix
                     || suffix == "veg" || suffix == "vet")
                {
                    library << path;
                }
            // a video first; a second .mp4 (a GoPro's) is taken for its data
            else if (videos.contains(suffix) && video.isEmpty())
                {
                    video = path;
                }
            else if (TelemetryImporter::canLoad(path) && data.isEmpty())
                {
                    data = path;
                }
            else
                {
                    unknown << info.fileName();
                }
        }
    if (!project.isEmpty())
        {
            openProject(project);
        }
    else
        {
            if (!video.isEmpty())
                {
                    openChosenVideo(video);
                }
            if (!data.isEmpty())
                {
                    openTelemetry(data);
                }
        }
    if (!library.isEmpty() && !m_libraryPanel->importDropped(library))
        {
            unknown << library;
        }
    if (!unknown.isEmpty())
        {
            statusBar()->showMessage(tr("Not opened, not something this program reads: %1").arg(unknown.join(", ")), 8000);
        }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave())
        {
            event->ignore();
            return;
        }
    if (AppSettings::saveWindowLayout())
        {
            QSettings settings;
            settings.setValue("mainwindow/geometry", saveGeometry());
            settings.setValue("mainwindow/state", saveState(kLayoutVersion));
        }
    QMainWindow::closeEvent(event);
}

QWidget *MainWindow::createCentralWidget()
{
    QWidget *central = new QWidget(this);
    m_canvas = new VideoCanvas(central);

    m_playButton = new QToolButton(central);
    m_playButton->setIcon(Icons::named("media-playback-start"));
    m_playButton->setToolTip(tr("Play / pause (Space)"));
    m_playButton->setEnabled(false);
    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::togglePlayback);

    m_seekSlider = new QSlider(Qt::Horizontal, central);
    m_seekSlider->setEnabled(false);
    m_seekSlider->setSingleStep(1000);
    m_seekSlider->setPageStep(10000);
    // covers dragging, clicks on the groove and the keyboard
    connect(m_seekSlider, &QSlider::actionTriggered, this, [this]()
    {
        m_playback->setPosition(m_seekSlider->sliderPosition());
    });

    m_timeLabel = new QLabel(central);

    m_offsetSpin = new QDoubleSpinBox(central);
    m_offsetSpin->setRange(-1e7, 1e7);
    m_offsetSpin->setDecimals(2);
    m_offsetSpin->setSingleStep(0.1);
    m_offsetSpin->setSuffix(tr(" s"));
    m_offsetSpin->setKeyboardTracking(false);
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onOffsetEdited);

    m_autoSyncButton = new QToolButton(central);
    m_autoSyncButton->setText(tr("Auto"));
    connect(m_autoSyncButton, &QToolButton::clicked, this, [this]()
    {
        m_project.syncIsManual = false;
        autoSync();
    });

    m_rateSpin = new QDoubleSpinBox(central);
    m_rateSpin->setRange(0.01, 3600.0);
    m_rateSpin->setDecimals(2);
    m_rateSpin->setValue(1.0);
    m_rateSpin->setPrefix(QStringLiteral("x"));
    m_rateSpin->setKeyboardTracking(false);
    m_rateSpin->setToolTip(tr("Seconds of the data in one second of the video: 1 for an ordinary video,\n"
                              "the camera's speed-up for a time-lapse (10, 30...)."));
    connect(m_rateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double rate)
    {
        m_project.syncRate = rate;
        setModified(true);
        m_canvas->setSync(m_project.sync());
        m_timelinePanel->setSync(m_project.sync());
    });

    showSyncExplanation(QString());

    QHBoxLayout *transport = new QHBoxLayout;
    transport->addWidget(m_playButton);
    transport->addWidget(m_seekSlider, 1);
    transport->addWidget(m_timeLabel);

    QHBoxLayout *sync = new QHBoxLayout;
    sync->addWidget(new QLabel(tr("Data offset:"), central));
    sync->addWidget(m_offsetSpin);
    sync->addWidget(m_autoSyncButton);
    sync->addWidget(new QLabel(tr("Time-lapse:"), central));
    sync->addWidget(m_rateSpin);
    sync->addStretch(1);

    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->addWidget(m_canvas, 1);
    layout->addLayout(transport);
    layout->addLayout(sync);
    return central;
}

void MainWindow::createDocks()
{
    m_sourcesDock = addPlaceholderDock("sourcesDock", tr("Sources"), Qt::LeftDockWidgetArea);
    m_libraryDock = addPlaceholderDock("libraryDock", tr("Gauge library"), Qt::LeftDockWidgetArea);
    m_propertiesDock = addPlaceholderDock("propertiesDock", tr("Properties"), Qt::RightDockWidgetArea);
    m_timelineDock = addPlaceholderDock("timelineDock", tr("Timeline"), Qt::BottomDockWidgetArea);
    m_lapsDock = addPlaceholderDock("lapsDock", tr("Laps"), Qt::RightDockWidgetArea);
    tabifyDockWidget(m_propertiesDock, m_lapsDock);
    m_propertiesDock->raise();
    m_timelinePanel = new TimelinePanel(m_timelineDock);
    m_timelineDock->setWidget(m_timelinePanel);
    connect(m_timelinePanel, &TimelinePanel::seekRequested, this, [this](double seconds)
    {
        m_playback->setPosition(qint64(std::llround(seconds * 1000.0)));
    });
    // the spin box does what typing the value would
    connect(m_timelinePanel, &TimelinePanel::offsetDragged, m_offsetSpin, &QDoubleSpinBox::setValue);
    connect(m_timelinePanel, &TimelinePanel::cutsChanged, this, &MainWindow::setCuts);
    m_compareDock = addPlaceholderDock("lapCompareDock", tr("Compare laps"), Qt::BottomDockWidgetArea);
    tabifyDockWidget(m_timelineDock, m_compareDock);
    m_timelineDock->raise();
    m_comparePanel = new LapComparePanel(m_compareDock);
    m_compareDock->setWidget(m_comparePanel);
    m_dataDock = addPlaceholderDock("dataDock", tr("Data"), Qt::BottomDockWidgetArea);
    tabifyDockWidget(m_compareDock, m_dataDock);
    m_timelineDock->raise();
    m_dataPanel = new DataPanel(m_dataDock);
    m_dataDock->setWidget(m_dataPanel);
    connect(m_dataPanel, &DataPanel::seekRequested, this, &MainWindow::seekTelemetryTime);
    // the route map is the Laps panel's: what is picked in the data is shown on it
    connect(m_dataPanel, &DataPanel::stretchPicked, this, [this](double start, double end)
    {
        m_lapsPanel->setHighlight(start, end);
    });
    connect(m_dataPanel, &DataPanel::editsChanged, this, &MainWindow::setDataEdits);
    connect(m_dataPanel, &DataPanel::cutVideoRequested, this, &MainWindow::cutVideoOfData);
    // one smoothing for all charts
    connect(m_dataPanel, &DataPanel::smoothingChanged, m_timelinePanel, &TimelinePanel::setSmoothing);
    connect(m_dataPanel, &DataPanel::smoothingChanged, m_comparePanel, &LapComparePanel::setSmoothing);
    connect(m_timelinePanel, &TimelinePanel::smoothingChanged, m_dataPanel, &DataPanel::setSmoothing);
    connect(m_timelinePanel, &TimelinePanel::smoothingChanged, m_comparePanel, &LapComparePanel::setSmoothing);
    connect(m_comparePanel, &LapComparePanel::smoothingChanged, m_dataPanel, &DataPanel::setSmoothing);
    connect(m_comparePanel, &LapComparePanel::smoothingChanged, m_timelinePanel, &TimelinePanel::setSmoothing);
    // beside the properties and the laps: tall rather than wide, it would take a lot of the
    // video's height at the bottom
    m_musicDock = addPlaceholderDock("musicDock", tr("Music"), Qt::RightDockWidgetArea);
    tabifyDockWidget(m_lapsDock, m_musicDock);
    m_propertiesDock->raise();
    m_timelineDock->raise();
    m_musicPanel = new MusicPanel(m_musicDock);
    m_musicDock->setWidget(m_musicPanel);
    connect(m_musicPanel, &MusicPanel::soundtrackEdited, this, &MainWindow::setSoundtrack);
    connect(m_musicPanel, &MusicPanel::volumesPreviewed, this, [this](const Soundtrack &soundtrack)
    {
        m_heardSoundtrack = soundtrack;
        syncMusic();
    });
    connect(m_musicPanel, &MusicPanel::seekRequested, this, [this](double seconds)
    {
        m_playback->setPosition(qint64(std::llround(seconds * 1000.0)));
    });
    // what goes on underneath, for finding out why something does not work on a system: hidden
    // until asked for
    m_diagnosticsDock = addPlaceholderDock("diagnosticsDock", tr("Diagnostics"), Qt::BottomDockWidgetArea);
    tabifyDockWidget(m_dataDock, m_diagnosticsDock);
    m_diagnosticsDock->setWidget(new DiagnosticsPanel([this]() { return playersState(); }, m_diagnosticsDock));
    m_diagnosticsDock->hide();
    m_timelineDock->raise();
    m_lapsPanel = new LapsPanel(m_lapsDock);
    m_lapsDock->setWidget(m_lapsPanel);
    m_lapsPanel->setUnits(m_units);
    connect(m_lapsPanel, &LapsPanel::lineRequested, this, &MainWindow::placeStartFinish);
    connect(m_lapsPanel, &LapsPanel::referenceChosen, this, [this](int lap)
    {
        const int before = m_project.referenceLap;
        if (before == lap)
            {
                return;
            }
        m_undoStack->push(new OverlayCommand(lap > 0 ? tr("Use lap %1 as the reference").arg(lap) : tr("The fastest lap as the reference"),
                                             [this, before, lap](bool forward)
        {
            m_project.referenceLap = forward ? lap : before;
            setModified(true);
            updateLaps();
        }));
    });
    connect(m_lapsPanel, &LapsPanel::mapChoiceChanged, this, &MainWindow::applyMapSettings);
    connect(m_lapsPanel, &LapsPanel::removeLineRequested, this, [this]()
    {
        setLapLines(StartFinishLine(), m_project.sectors, tr("Remove start/finish line"));
    });
    connect(m_lapsPanel, &LapsPanel::sectorRequested, this, &MainWindow::placeSector);
    connect(m_lapsPanel, &LapsPanel::removeSectorsRequested, this, [this]()
    {
        setLapLines(m_project.startFinish, {}, tr("Remove sector lines"));
    });
    connect(m_lapsPanel, &LapsPanel::surfaceRequested, this, &MainWindow::markSurface);
    connect(m_lapsPanel, &LapsPanel::surfacesEdited, this, [this](const QVector<SurfaceStretch> &surfaces)
    {
        setSurfaces(surfaces, tr("Change surfaces"));
    });
    connect(m_lapsPanel, &LapsPanel::removeSurfacesRequested, this, [this]()
    {
        setSurfaces({}, tr("Clear surfaces"));
    });
    connect(m_lapsPanel, &LapsPanel::seekRequested, this, &MainWindow::seekTelemetryTime);
    connect(m_lapsPanel, &LapsPanel::lapsSelected, this, [this](const QVector<int> &laps)
    {
        m_comparePanel->setLaps(laps);
        if (laps.size() >= 2)
            {
                m_compareDock->show();
                m_compareDock->raise();
            }
    });
    tabifyDockWidget(m_sourcesDock, m_libraryDock);
    m_sourcesDock->raise();
    m_libraryPanel = new GaugeLibraryPanel(m_libraryDock);
    m_libraryDock->setWidget(m_libraryPanel);
    // scrolls rather than making the window wider than the screen
    QScrollArea *propertiesScroll = new QScrollArea(m_propertiesDock);
    propertiesScroll->setWidgetResizable(true);
    propertiesScroll->setFrameShape(QFrame::NoFrame);
    propertiesScroll->setWidget(createPropertiesPanel());
    m_propertiesDock->setWidget(propertiesScroll);

    QWidget *sources = new QWidget(m_sourcesDock);
    m_sourcesLabel = new QLabel(sources);
    m_sourcesLabel->setTextFormat(Qt::PlainText);
    m_sourcesLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_sourcesLabel->setWordWrap(true);
    m_sourcesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_channelTree = new QTreeWidget(sources);
    m_channelTree->setHeaderLabels({tr("Channel"), tr("Now"), tr("Unit"), tr("Min"), tr("Max")});
    m_channelTree->setRootIsDecorated(false);
    m_channelTree->setUniformRowHeights(true);
    m_channelTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_channelTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_channelTree->header()->setStretchLastSection(false);
    m_channelTree->setToolTip(tr("\"Now\" is the value at the video frame on screen"));

    m_elevationCombo = new QComboBox(sources);
    m_elevationCombo->addItem(tr("Recorded / terrain model"), int(Terrain::WhenMissing));
    m_elevationCombo->addItem(tr("Terrain model"), int(Terrain::Always));
    m_elevationCombo->addItem(tr("Recorded only"), int(Terrain::Never));
    m_elevationCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_elevationCombo->setToolTip(tr("Where the elevation comes from. The terrain model gives the height of the ground under "
                                    "the route (%1); grade, climb and vertical speed follow it. Its pieces are fetched once, "
                                    "like a map's.").arg(Terrain::attribution()));
    connect(m_elevationCombo, &QComboBox::activated, this, [this]()
    {
        const Terrain::Use chosen = Terrain::Use(m_elevationCombo->currentData().toInt());
        if (chosen != m_project.elevation)
            {
                m_project.elevation = chosen;
                setModified(true);
                recomputeData();
            }
    });
    QHBoxLayout *elevationRow = new QHBoxLayout;
    elevationRow->addWidget(new QLabel(tr("Elevation:"), sources));
    elevationRow->addWidget(m_elevationCombo, 1);

    // the wind of this ride: the weather history's (when the settings ask for it), or the user's own
    m_windCombo = new QComboBox(sources);
    m_windCombo->addItem(tr("Weather history"), false);
    m_windCombo->addItem(tr("By hand:"), true);
    // the panel may be narrow: the combo gives way rather than the panel growing
    m_windCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_windCombo->setToolTip(tr("Where the wind of this ride comes from. Weather history is switched on in the settings "
                               "(Weather); a wind entered here by hand is the same all along the ride and kept in the project."));
    m_windSpeedSpin = new QDoubleSpinBox(sources);
    m_windSpeedSpin->setRange(0.0, 200.0);
    m_windSpeedSpin->setDecimals(0);
    m_windSpeedSpin->setToolTip(tr("The wind's speed as forecasts give it, 10 m above the ground"));
    m_windFromSpin = new QDoubleSpinBox(sources);
    m_windFromSpin->setRange(0.0, 359.0);
    m_windFromSpin->setDecimals(0);
    m_windFromSpin->setSingleStep(5.0);
    m_windFromSpin->setWrapping(true);
    m_windFromSpin->setPrefix(tr("from "));
    m_windFromSpin->setSuffix(QStringLiteral("°"));
    m_windFromSpin->setToolTip(tr("Where the wind blows from: 0 north, 90 east, 180 south, 270 west"));
    const auto windChanged = [this]()
    {
        HandWind wind;
        wind.used = m_windCombo->currentData().toBool();
        wind.speed = m_windSpeedSpin->value() / m_units.speedScale();
        wind.from = m_windFromSpin->value();
        m_windSpeedSpin->setEnabled(wind.used);
        m_windFromSpin->setEnabled(wind.used);
        // numbers typed while the weather history is in use are kept, but change nothing yet
        const bool matters = wind.used || m_project.wind.used;
        if (wind != m_project.wind)
            {
                m_project.wind = wind;
                if (matters)
                    {
                        setModified(true);
                        recomputeData();
                    }
            }
    };
    connect(m_windCombo, &QComboBox::activated, this, windChanged);
    connect(m_windSpeedSpin, &QDoubleSpinBox::editingFinished, this, windChanged);
    connect(m_windFromSpin, &QDoubleSpinBox::editingFinished, this, windChanged);
    // the numbers on a line of their own, under the choice, so the panel can stay narrow
    QGridLayout *windRow = new QGridLayout;
    windRow->setColumnStretch(1, 1);
    windRow->addWidget(new QLabel(tr("Wind:"), sources), 0, 0);
    windRow->addWidget(m_windCombo, 0, 1, 1, 2);
    windRow->addWidget(m_windSpeedSpin, 1, 1);
    windRow->addWidget(m_windFromSpin, 1, 2);
    showWind();

    QVBoxLayout *sourcesLayout = new QVBoxLayout(sources);
    sourcesLayout->setContentsMargins(6, 6, 6, 6);
    sourcesLayout->addWidget(m_sourcesLabel);
    sourcesLayout->addLayout(elevationRow);
    sourcesLayout->addLayout(windRow);
    sourcesLayout->addWidget(m_channelTree, 1);
    m_sourcesDock->setWidget(sources);
}

void MainWindow::showWind()
{
    const QSignalBlocker combo(m_windCombo);
    const QSignalBlocker speed(m_windSpeedSpin);
    const QSignalBlocker from(m_windFromSpin);
    m_windCombo->setCurrentIndex(m_project.wind.used ? 1 : 0);
    m_windSpeedSpin->setSuffix(" " + m_units.speedLabel());
    m_windSpeedSpin->setValue(m_project.wind.speed * m_units.speedScale());
    m_windFromSpin->setValue(m_project.wind.from);
    m_windSpeedSpin->setEnabled(m_project.wind.used);
    m_windFromSpin->setEnabled(m_project.wind.used);
}

QDockWidget *MainWindow::addPlaceholderDock(const QString &objectName, const QString &title, Qt::DockWidgetArea area)
{
    QDockWidget *dock = new QDockWidget(title, this);
    // objectName is what saveState()/restoreState() key on - must stay untranslated
    dock->setObjectName(objectName);
    dock->setWidget(new QWidget(dock));
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *openVideoAction = fileMenu->addAction(tr("Open &video..."), this, &MainWindow::chooseVideo);
    openVideoAction->setShortcut(QKeySequence::Open);
    openVideoAction->setIcon(Icons::named("video-x-generic"));
    addRecentMenu(fileMenu, tr("&Recent videos"), AppSettings::RecentVideos, [this](const QString &path)
    {
        openChosenVideo(path);
    });
    QAction *openDataAction = fileMenu->addAction(tr("Open &data..."), this, &MainWindow::chooseTelemetry);
    openDataAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    openDataAction->setIcon(Icons::named("x-office-spreadsheet"));
    addRecentMenu(fileMenu, tr("Recent &data"), AppSettings::RecentData, [this](const QString &path)
    {
        openTelemetry(path);
    });
    m_dataSourcesAction = fileMenu->addAction(tr("Add data from other &files..."), this, &MainWindow::editDataSources);
    m_dataSourcesAction->setEnabled(false);
    m_dataSourcesAction->setIcon(Icons::named("list-add"));
    m_saveDataAction = fileMenu->addAction(tr("Save data as..."), this, &MainWindow::saveData);
    m_saveDataAction->setToolTip(tr("The data with its corrections, as GPX or CSV"));
    m_saveDataAction->setEnabled(false);
    fileMenu->addAction(tr("&Join videos..."), this, [this]() { joinVideos(); });
    QAction *backgroundAction = fileMenu->addAction(tr("&Without a video..."), this, &MainWindow::chooseBackground);
    backgroundAction->setToolTip(tr("The gauges over a plain color, or see-through, as long as the data"));
    fileMenu->addSeparator();
    QAction *openProjectAction = fileMenu->addAction(tr("Open &project..."), this, &MainWindow::chooseProject);
    openProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    openProjectAction->setIcon(Icons::named("document-open"));
    addRecentMenu(fileMenu, tr("Recent pro&jects"), AppSettings::RecentProjects, [this](const QString &path)
    {
        openProject(path);
    });
    QAction *saveAction = fileMenu->addAction(tr("&Save project"), this, &MainWindow::saveProject);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setIcon(Icons::named("document-save"));
    QAction *saveAsAction = fileMenu->addAction(tr("Save project &as..."), this, &MainWindow::saveProjectAs);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setIcon(Icons::named("document-save-as"));
    fileMenu->addSeparator();
    m_exportAction = fileMenu->addAction(tr("&Export video..."), this, &MainWindow::exportVideo);
    m_exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_exportAction->setIcon(Icons::named("document-export", "media-record"));
    m_exportAction->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Save the &gauges as a template..."), this, &MainWindow::saveAsTemplate);
    fileMenu->addSeparator();
    QAction *settingsAction = fileMenu->addAction(tr("&Settings..."), this, &MainWindow::showSettings);
    settingsAction->setShortcut(QKeySequence::Preferences);
    settingsAction->setIcon(Icons::named("preferences-system"));
    // macOS moves it to the application menu, where it is expected there
    settingsAction->setMenuRole(QAction::PreferencesRole);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setIcon(Icons::named("application-exit"));

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setIcon(Icons::named("edit-undo"));
    editMenu->addAction(undoAction);
    QAction *redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setIcon(Icons::named("edit-redo"));
    editMenu->addAction(redoAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_sourcesDock->toggleViewAction());
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_timelineDock->toggleViewAction());
    viewMenu->addAction(m_compareDock->toggleViewAction());
    viewMenu->addAction(m_dataDock->toggleViewAction());
    viewMenu->addAction(m_lapsDock->toggleViewAction());
    viewMenu->addAction(m_musicDock->toggleViewAction());
    viewMenu->addAction(m_diagnosticsDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(Icons::named("view-refresh"), tr("&Reset layout"), this, [this]()
    {
        restoreState(m_defaultLayout, kLayoutVersion);
        // a dock closed before is brought back with the rest
        for (QDockWidget *dock : findChildren<QDockWidget *>())
            {
                dock->setFloating(false);
            }
        m_lapsDock->raise();
    });

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(Icons::named("help-contents", "help-about"), tr("User &manual"), this,
                        []() { QDesktopServices::openUrl(QUrl(kManual)); });
    helpMenu->addAction(Icons::named("help-about"), tr("&About"), this, &MainWindow::showAbout);
    helpMenu->addAction(Icons::named("view-refresh"), tr("Check for &updates..."), this, [this]() { checkForUpdates(true); });
    m_updateAction = helpMenu->addAction(Icons::named("go-next"), QString(), this, &MainWindow::showNewRelease);
    m_updateAction->setVisible(false);
    // to the README, which anyone can check, not to a fund: a link in the program could be swapped
    helpMenu->addAction(QIcon(":/images/icons/flag-ukraine.svg"), tr("Support &Ukraine"), this,
                        []() { QDesktopServices::openUrl(QUrl(kSupportUkraine)); });
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::chooseVideo()
{
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open video"), settings.value("paths/video").toString(),
                         tr("Video files (%1);;All files (*)").arg("*.mp4 *.mov *.mkv *.avi *.m4v *.mts *.webm"));
    if (!path.isEmpty())
        {
            settings.setValue("paths/video", QFileInfo(path).absolutePath());
            openChosenVideo(path);
        }
}

void MainWindow::openChosenVideo(const QString &path)
{
    // a camera splits a long recording into chapters: offered as the one video it is
    const QStringList chapters = VideoJoin::chaptersFrom(path);
    if (chapters.size() > 1 && QFileInfo(chapters.first()) == QFileInfo(path)
            && QMessageBox::question(this, tr("Open video"),
                                     tr("%1 is the first of %n parts of one recording. Join them into one video?",
                                        nullptr, int(chapters.size())).arg(QFileInfo(path).fileName()))
            == QMessageBox::Yes)
        {
            joinVideos(chapters);
            return;
        }
    openVideo(path);
}

QMenu *MainWindow::addRecentMenu(QMenu *parent, const QString &title, AppSettings::RecentKind kind,
                                 const std::function<void(const QString &)> &open)
{
    QMenu *menu = parent->addMenu(Icons::named("document-open-recent", "document-open"), title);
    menu->setToolTipsVisible(true);
    // filled each time it opens: other windows and the settings change the lists
    connect(menu, &QMenu::aboutToShow, this, [this, menu, kind, open]()
    {
        menu->clear();
        int number = 0;
        for (const QString &path : AppSettings::recentFiles(kind))
            {
                // a file moved or deleted since is left out
                if (!QFileInfo::exists(path))
                    {
                        continue;
                    }
                ++number;
                // the first nine reachable by their digit
                const QString name = QFileInfo(path).fileName().replace('&', "&&");
                QAction *action = menu->addAction(number <= 9 ? QString("&%1  %2").arg(number).arg(name) : name);
                action->setToolTip(QDir::toNativeSeparators(path));
                connect(action, &QAction::triggered, this, [open, path]()
                {
                    open(path);
                });
            }
        if (number == 0)
            {
                menu->addAction(tr("None"))->setEnabled(false);
                return;
            }
        menu->addSeparator();
        menu->addAction(Icons::named("edit-clear"), tr("Clear the list"), this, [kind]()
        {
            AppSettings::clearRecentFiles(kind);
        });
    });
    return menu;
}

void MainWindow::chooseTelemetry()
{
    QSettings settings;
    // data usually lies next to the video
    const QString startDir = settings.value("paths/telemetry", settings.value("paths/video")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open data"), startDir, TelemetryImporter::fileDialogFilter());
    if (!path.isEmpty())
        {
            settings.setValue("paths/telemetry", QFileInfo(path).absolutePath());
            openTelemetry(path);
        }
}

void MainWindow::openVideo(const QString &path, const QVector<TimeShift> &shifts)
{
    m_project.syncIsManual = false;
    // a time-lapse is the video's: another one starts as an ordinary video
    m_project.syncRate = 1.0;
    // what was cut and where breaks were belong to the video before
    m_project.videoShifts = shifts;
    m_project.cuts.clear();
    m_timelinePanel->setCuts(m_project.cuts);
    m_musicPanel->setCuts(m_project.cuts);
    loadVideo(path);
    AppSettings::addRecentFile(AppSettings::RecentVideos, path);
    // a GoPro's video holds its positions: they are the data, unless some is open already
    if (m_project.telemetryPath.isEmpty() && m_pendingTelemetryPath.isEmpty()
            && QFileInfo(path).suffix().compare("mp4", Qt::CaseInsensitive) == 0)
        {
            loadTelemetry(path, false, true);
        }
}

void MainWindow::joinVideos(const QStringList &paths)
{
    JoinDialog dialog(paths, this);
    if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
    openVideo(dialog.outputPath(), dialog.shifts());
    setModified(true);
    if (!dialog.shifts().isEmpty())
        {
            statusBar()->showMessage(tr("Joined; the data follows the %n break(s) between the recordings.", nullptr,
                                        int(dialog.shifts().size())), 8000);
        }
}

void MainWindow::loadVideo(const QString &path)
{
    m_project.videoPath = path;
    m_canvas->clear();
    m_project.background = VideoBackground();
    m_playback->setClock(false);
    m_player->setSource(QUrl::fromLocalFile(path));
    // shows the first frame without starting to play
    m_player->pause();
    m_playButton->setEnabled(true);
    m_exportAction->setEnabled(true);
    updateSourcesInfo();
    autoSync();
}

void MainWindow::askFlyoverCamera(int index)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    const GaugeComponent::FlyoverCamera current = m_canvas->overlay().flyoverCamera(index);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("3D flyover camera"));
    QFormLayout *form = new QFormLayout(&dialog);
    QSpinBox *tilt = new QSpinBox(&dialog);
    tilt->setRange(10, 85);
    tilt->setSuffix(QStringLiteral("°"));
    tilt->setValue(int(std::lround(current.tilt)));
    tilt->setToolTip(tr("From looking straight down: more looks further ahead, towards the horizon"));
    QSpinBox *height = new QSpinBox(&dialog);
    height->setRange(10, 20000);
    height->setSingleStep(25);
    height->setSuffix(tr(" m"));
    height->setValue(int(std::lround(current.height)));
    height->setToolTip(tr("Above the rider; the camera is behind them as far as its tilt takes it"));
    QDoubleSpinBox *hills = new QDoubleSpinBox(&dialog);
    hills->setRange(1.0, 5.0);
    hills->setSingleStep(0.5);
    hills->setDecimals(1);
    hills->setPrefix(tr("x "));
    hills->setValue(current.hills);
    hills->setToolTip(tr("Hills higher than they are: true ones look flat from above"));
    form->addRow(tr("Tilt:"), tilt);
    form->addRow(tr("Height:"), height);
    form->addRow(tr("Hills:"), hills);
    QPushButton *usual = new QPushButton(tr("As the gauge has it"), &dialog);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->addButton(usual, QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool reset = false;
    connect(usual, &QPushButton::clicked, &dialog, [&]() { reset = true; dialog.accept(); });
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].flyoverCamera = reset ? GaugeComponent::FlyoverCamera{0.0, 0.0, 0.0}
                                       : GaugeComponent::FlyoverCamera{double(tilt->value()), double(height->value()), hills->value()};
    changeOverlay(tr("Set the 3D flyover's camera"), after);
}

void MainWindow::chooseBackground()
{
    BackgroundDialog dialog(m_project.background, this);
    if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
    if (!m_project.videoPath.isEmpty()
            && QMessageBox::question(this, dialog.windowTitle(), tr("The video %1 is put aside for the background. Go on?")
                                     .arg(QFileInfo(m_project.videoPath).fileName())) != QMessageBox::Yes)
        {
            return;
        }
    useBackground(dialog.background());
    setModified(true);
    if (!m_session)
        {
            statusBar()->showMessage(tr("Open the data now: the picture is as long as it."), 8000);
        }
}

void MainWindow::useBackground(const VideoBackground &background)
{
    m_project.videoPath.clear();
    m_project.background = background;
    m_player->setSource(QUrl());
    m_canvas->clear();
    m_canvas->setBackground(background);
    m_playback->clock()->setRate(background.fps);
    m_playback->setClock(true);
    m_playback->clock()->setDuration(m_session ? qint64(std::llround(m_session->duration() * 1000.0)) : 0);
    m_playback->setPosition(0);
    m_playButton->setEnabled(true);
    m_exportAction->setEnabled(true);
    // the picture runs on the data's own time: nothing to match
    m_project.videoShifts.clear();
    m_project.syncIsManual = false;
    setSyncOffset(0.0, tr("No video: the gauges follow the data's own time."));
    updateSourcesInfo();
}

void MainWindow::openTelemetry(const QString &path)
{
    // corrections and added files belong to the data before
    m_project.dataEdits.clear();
    m_project.moreData.clear();
    m_dataPanel->setEdits(m_project.dataEdits);
    loadTelemetry(path, false);
    // kept among the recent ones once it is read
    m_pendingRecentData = true;
}

void MainWindow::loadTelemetry(const QString &path, bool keepSync, bool quietly)
{
    m_pendingQuietly = quietly;
    m_pendingRecentData = false;
    m_pendingTelemetryPath = path;
    m_pendingKeepsSync = keepSync;
    // only the last load counts, also when the same file is loaded again before the first is done
    const quint64 generation = ++m_importGeneration;
    statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(path).fileName()));

    const QVector<DataEdit> edits = m_project.dataEdits;
    const QVector<DataSource> sources = m_project.moreData;
    const Terrain::Use elevation = m_project.elevation;
    const PowerEstimate::Rider rider = m_rider;
    const WeatherChoice weather = m_weather;
    const QVector<SurfaceStretch> surfaces = m_project.surfaces;
    const HandWind wind = m_project.wind;
    const Drivetrain drivetrain = m_drivetrain;
    auto *watcher = new QFutureWatcher<ImportResult>(this);
    connect(watcher, &QFutureWatcher<ImportResult>::finished, this,
            [this, watcher, path, generation, edits, elevation, rider, weather, surfaces, wind, drivetrain]()
    {
        const ImportResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_importGeneration)
            {
                return;
            }
        m_terrainWaiting = result.waiting;
        m_weatherWaiting = result.weatherWaiting;
        m_mainSession = result.main;
        // the shifts the clocks gave; the files and the choices are those still in the project
        // (another load follows any change of them)
        if (result.session && result.sources.size() == m_project.moreData.size())
            {
                m_project.moreData = result.sources;
            }
        telemetryImported(path, result.raw, result.session, result.error);
        if (m_terrainWaiting || m_weatherWaiting)
            {
                // what was asked for may have come already, while the data was worked out
                m_sinceRecompute.start();
                m_arrivalTimer->start();
            }
        // what was changed while it was worked out (the elevation, the wind, an edit...) counts too
        const bool changed = edits != m_project.dataEdits || elevation != m_project.elevation || rider != m_rider
                             || weather != m_weather || surfaces != m_project.surfaces || wind != m_project.wind
                             || drivetrain != m_drivetrain;
        if (result.session && changed)
            {
                recomputeData();
            }
    });
    watcher->setFuture(QtConcurrent::run([path, edits, sources, elevation, rider, surfaces, weather, wind, drivetrain]()
    {
        ImportResult result;
        result.main = TelemetryImporter::loadRecorded(path, &result.error);
        result.sources = sources;
        result.raw = DataSources::loadAndMerge(result.main, &result.sources);
        result.session = ProjectData::workedOut(result.raw, edits, elevation, rider, surfaces, weather, wind, drivetrain, &result.waiting,
                                   &result.weatherWaiting);
        return result;
    }));
}

void MainWindow::chooseProject()
{
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), settings.value("paths/project").toString(),
                         tr("Projects (*.%1);;All files (*)").arg(ProjectFile::Suffix));
    if (!path.isEmpty())
        {
            settings.setValue("paths/project", QFileInfo(path).absolutePath());
            openProject(path);
        }
}

void MainWindow::openProject(const QString &path)
{
    if (!maybeSave())
        {
            return;
        }
    Project project;
    QString error;
    if (!ProjectFile::load(path, &project, &error))
        {
            QMessageBox::warning(this, tr("Open project"), error);
            return;
        }
    QStringList missing;
    for (QString *file : {&project.videoPath, &project.telemetryPath})
        {
            if (!file->isEmpty() && !QFileInfo::exists(*file))
                {
                    missing << QDir::toNativeSeparators(*file);
                    file->clear();
                }
        }
    // added data that is not there stays, and is a warning of the data
    for (const DataSource &source : project.moreData)
        {
            if (!QFileInfo::exists(source.path))
                {
                    missing << QDir::toNativeSeparators(source.path);
                }
        }
    // music that is not there stays in the list, shown as missing, and is left out of the export
    for (const MusicTrack &track : project.soundtrack.tracks)
        {
            if (!QFileInfo::exists(track.path))
                {
                    missing << QDir::toNativeSeparators(track.path);
                }
        }

    m_project = project;
    m_projectPath = path;
    AppSettings::addRecentFile(AppSettings::RecentProjects, path);
    m_undoStack->clear();
    setModified(false);
    m_session.reset();
    m_recordedSession.reset();
    m_rawSession.reset();
    m_mainSession.reset();
    m_dataSourcesAction->setEnabled(false);
    m_saveDataAction->setEnabled(false);
    m_dataPanel->setSessions(nullptr, nullptr, m_units);
    m_lapsPanel->setSession(nullptr, m_project.startFinish, m_project.sectors);
    m_lapsPanel->setSurfaces(m_project.surfaces);
    m_timelinePanel->setSession(nullptr, m_units);
    m_comparePanel->setSession(nullptr, m_units);
    m_timelinePanel->setCuts(m_project.cuts);
    m_musicPanel->setCuts(m_project.cuts);
    m_musicPanel->startOver(m_project.soundtrack);
    m_heardSoundtrack = m_project.soundtrack;
    m_elevationCombo->setCurrentIndex(m_elevationCombo->findData(int(m_project.elevation)));
    showWind();
    m_dataPanel->setEdits(m_project.dataEdits);
    m_pendingTelemetryPath.clear();
    m_canvas->clear();
    m_playback->setClock(false);
    m_player->setSource(QUrl());
    m_playButton->setEnabled(false);
    m_exportAction->setEnabled(false);
    rebuildOverlay();
    setSyncOffset(m_project.syncOffset, m_project.syncIsManual ? tr("Set by hand.") : QString());
    if (!m_project.videoPath.isEmpty())
        {
            loadVideo(m_project.videoPath);
        }
    else if (m_project.background.isOn())
        {
            useBackground(m_project.background);
        }
    if (!m_project.telemetryPath.isEmpty())
        {
            loadTelemetry(m_project.telemetryPath, true);
        }
    updateSourcesInfo();
    updateWindowTitle();
    if (!missing.isEmpty())
        {
            QMessageBox::warning(this, tr("Open project"),
                                 tr("These files of the project were not found:\n%1\n\n"
                                    "Open them again from where they are now and save the project.").arg(missing.join('\n')));
        }
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        {
            return saveProjectAs();
        }
    QString error;
    if (!ProjectFile::save(m_projectPath, m_project, &error))
        {
            QMessageBox::warning(this, tr("Save project"), error);
            return false;
        }
    setModified(false);
    statusBar()->showMessage(tr("Project saved."), 3000);
    return true;
}

bool MainWindow::saveProjectAs()
{
    QSettings settings;
    // next to the video, named after it: the usual place to look for it later
    QString suggestion = m_projectPath;
    if (suggestion.isEmpty() && !m_project.videoPath.isEmpty())
        {
            const QFileInfo video(m_project.videoPath);
            suggestion = video.absolutePath() + "/" + video.completeBaseName() + "." + ProjectFile::Suffix;
        }
    if (suggestion.isEmpty())
        {
            suggestion = settings.value("paths/project").toString();
        }
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"), suggestion,
                   tr("Projects (*.%1)").arg(ProjectFile::Suffix));
    if (path.isEmpty())
        {
            return false;
        }
    if (QFileInfo(path).suffix().isEmpty())
        {
            path += QString(".") + ProjectFile::Suffix;
        }
    settings.setValue("paths/project", QFileInfo(path).absolutePath());
    const QString previous = m_projectPath;
    m_projectPath = path;
    if (!saveProject())
        {
            m_projectPath = previous;
            return false;
        }
    updateWindowTitle();
    AppSettings::addRecentFile(AppSettings::RecentProjects, path);
    return true;
}

void MainWindow::exportVideo()
{
    if (!m_project.hasPicture())
        {
            return;
        }
    if (!m_session)
        {
            QMessageBox::information(this, tr("Export video"),
                                     tr("Open the data for this video first: without it there is nothing to draw."));
            return;
        }
    const int pending = TileStore::pendingOfAllMaps();
    if (pending > 0 || m_terrainWaiting || m_weatherWaiting)
        {
            // what is drawn before they come stays as it is in the exported file
            const QString what = pending > 0 ? tr("%n piece(s) of the map under the gauges are still being fetched.", nullptr, pending)
                                 : m_terrainWaiting ? tr("The heights of the terrain are still being fetched.")
                                 : tr("The weather of the ride is still being fetched.");
            if (QMessageBox::question(this, tr("Export video"),
                                      tr("%1\nAn export started now would be drawn without them. Export anyway?").arg(what))
                    != QMessageBox::Yes)
                {
                    return;
                }
        }
    // two decoders on one file only fight for the disk and the graphics card
    m_playback->pause();
    Soundtrack soundtrack = m_project.soundtrack;
    soundtrack.tracks.erase(std::remove_if(soundtrack.tracks.begin(), soundtrack.tracks.end(), [](const MusicTrack &track)
    {
        return !QFileInfo::exists(track.path);
    }), soundtrack.tracks.end());
    ExportDialog dialog(m_project.videoPath, Overlay(m_project.overlay, m_session, m_units, m_project.theme),
                        m_project.sync(), m_project.cuts, soundtrack, m_playback->position() / 1000.0, this);
    if (m_project.videoPath.isEmpty())
        {
            dialog.setBackground(m_project.background, m_playback->duration() / 1000.0, m_project.telemetryPath);
        }
    dialog.setLaps(LapExport::ranges(*m_session, m_project.sync(), m_playback->duration() / 1000.0));
    dialog.exec();
}

void MainWindow::rebuildOverlay()
{
    // with a background the picture is as long as the data, whatever was done to it
    if (m_playback->isClock())
        {
            m_playback->clock()->setDuration(m_session ? qint64(std::llround(m_session->duration() * 1000.0)) : 0);
        }
    m_canvas->setOverlay(Overlay(m_project.overlay, m_session, m_units, m_project.theme));
    updateProperties();
}

void MainWindow::updateWindowTitle()
{
    const QString appName = qApp->property("appname").toString();
    const QString project = m_projectPath.isEmpty() ? QString() : QFileInfo(m_projectPath).completeBaseName();
    // "[*]" is where Qt puts the mark for unsaved changes
    setWindowTitle(project.isEmpty() ? appName + "[*]" : QString("%1[*] - %2").arg(project, appName));
    setWindowModified(m_modified);
}

void MainWindow::setModified(bool modified)
{
    m_modified = modified;
    setWindowModified(modified);
}

bool MainWindow::maybeSave()
{
    if (!m_modified)
        {
            return true;
        }
    const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Unsaved changes"),
            tr("The gauges or the synchronization were changed. Save the project?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        {
            return saveProject();
        }
    return answer == QMessageBox::Discard;
}

// --- editing the overlay ---------------------------------------------------

void MainWindow::applyOverlayState(const OverlayState &state)
{
    m_project.overlay = state.items;
    m_project.theme = state.theme;
    setModified(true);
    rebuildOverlay();
}

void MainWindow::changeOverlay(const QString &description, const OverlayState &after)
{
    const OverlayState before{m_project.overlay, m_project.theme};
    if (before.items == after.items && before.theme == after.theme)
        {
            return;
        }
    // pushing runs redo(), which applies `after`
    m_undoStack->push(new OverlayCommand(description, [this, before, after](bool forward)
    {
        applyOverlayState(forward ? after : before);
    }));
}

void MainWindow::fitLayoutTo(double aspect)
{
    const double was = m_project.layoutAspect;
    if (aspect <= 0.0 || std::abs(aspect - was) < 0.01)
        {
            return;
        }
    m_project.layoutAspect = aspect;
    if (was <= 0.0 || m_project.overlay.isEmpty())
        {
            return;   // a project from before: its gauges were placed on this video, most likely
        }
    // measured from their nearest edges on the old shape, put back the same way on the new one;
    // their size follows the height, as it does when a template is applied
    const Overlay placed(m_project.overlay, nullptr, m_units, m_project.theme);
    const QVector<OverlayItem> fitted = GaugeTemplate::fromOverlay(placed, m_project.theme, was).overlayItems(aspect);
    changeOverlay(tr("Place the gauges for the video's shape"), {fitted, m_project.theme});
}

void MainWindow::addGauge(const QString &gaugeId)
{
    addGaugeAt(gaugeId, QPointF(-1.0, -1.0));
}

void MainWindow::addGaugeAt(const QString &gaugeId, const QPointF &center)
{
    const Gauge gauge = GaugeLibrary::gauge(gaugeId);
    if (gauge.isEmpty())
        {
            return;
        }
    OverlayItem item;
    item.gaugeId = gaugeId;
    item.channel = GaugeLibrary::defaultChannel(gauge, m_session ? m_session->channelIds() : QStringList());
    // near the middle, each new one a little further down right so they do not hide each other
    const double step = 0.03 * (m_project.overlay.size() % 8);
    // a quarter of the picture's height, a wide gauge (a sector bar) no wider than about half of it
    const double picture = m_canvas->pictureAspect() > 0.0 ? m_canvas->pictureAspect() : 16.0 / 9.0;
    const double shape = gauge.size.width() / gauge.size.height();
    item.height = std::min(0.25, 0.45 * picture / shape);
    const double width = item.height * shape / picture;
    item.x = std::min(0.35 + step, 1.0 - width);
    item.y = std::min(0.3 + step, 1.0 - item.height);
    if (center.x() >= 0.0 && center.y() >= 0.0)
        {
            // where it was dropped, the whole of it on the picture
            item.x = std::clamp(center.x() - width / 2.0, 0.0, std::max(0.0, 1.0 - width));
            item.y = std::clamp(center.y() - item.height / 2.0, 0.0, std::max(0.0, 1.0 - item.height));
        }

    OverlayState after{m_project.overlay, m_project.theme};
    after.items.append(item);
    changeOverlay(tr("Add gauge"), after);
    m_canvas->setSelectedItem(int(m_project.overlay.size()) - 1);
}

void MainWindow::applyTemplate(const QString &templateId)
{
    const GaugeTemplate t = TemplateLibrary::get(templateId);
    if (t.items.isEmpty())
        {
            return;
        }
    // laid out for the shape of this video; 16:9 until one is open
    const double aspect = m_canvas->pictureAspect() > 0.0 ? m_canvas->pictureAspect() : 16.0 / 9.0;
    m_project.layoutAspect = aspect;
    m_canvas->setSelectedItem(-1);
    changeOverlay(tr("Apply template"), {t.overlayItems(aspect), t.theme});
}

void MainWindow::saveAsTemplate()
{
    if (m_project.overlay.isEmpty())
        {
            QMessageBox::information(this, tr("Save as template"), tr("There are no gauges on the video to save."));
            return;
        }
    // a name, and a category out of those there are or a new one
    QStringList categories;
    QStringList taken;
    for (const TemplateLibrary::Entry &entry : TemplateLibrary::entries())
        {
            if (!entry.category.isEmpty() && !categories.contains(entry.category))
                {
                    categories << entry.category;
                }
            if (!entry.builtin)
                {
                    taken << entry.name.toLower();
                }
        }
    categories.sort(Qt::CaseInsensitive);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Save as template"));
    QLineEdit *nameEdit = new QLineEdit(&dialog);
    QComboBox *categoryCombo = new QComboBox(&dialog);
    categoryCombo->setEditable(true);
    categoryCombo->addItems(categories);
    categoryCombo->setCurrentText(QSettings().value("templates/lastCategory").toString());
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QPushButton *save = buttons->button(QDialogButtonBox::Save);
    save->setEnabled(false);
    connect(nameEdit, &QLineEdit::textChanged, save, [save](const QString &text) { save->setEnabled(!text.trimmed().isEmpty()); });
    QFormLayout *form = new QFormLayout(&dialog);
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Category:"), categoryCombo);
    form->addRow(new QLabel(tr("The gauges on the video, where they are and their colors.\n"
                               "It appears among the templates in the library."), &dialog));
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
    const QString name = nameEdit->text().trimmed();
    if (taken.contains(name.toLower())
            && QMessageBox::question(this, tr("Save as template"), tr("There is a template \"%1\" already. Replace it?").arg(name))
            != QMessageBox::Yes)
        {
            return;
        }
    const double aspect = m_canvas->pictureAspect() > 0.0 ? m_canvas->pictureAspect() : 16.0 / 9.0;
    GaugeTemplate made = GaugeTemplate::fromOverlay(m_canvas->overlay(), m_project.theme, aspect);
    made.name = name;
    made.category = categoryCombo->currentText().trimmed();
    QSettings().setValue("templates/lastCategory", made.category);
    QString error;
    if (TemplateLibrary::add(made, &error).isEmpty())
        {
            QMessageBox::warning(this, tr("Save as template"), error);
            return;
        }
    m_libraryPanel->reload();
    statusBar()->showMessage(tr("Template \"%1\" saved to the library").arg(name), 5000);
}

void MainWindow::removeGauge(int index)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items.removeAt(index);
    m_canvas->setSelectedItem(-1);
    changeOverlay(tr("Remove gauge"), after);
}

void MainWindow::onItemGeometryEdited(int index, double x, double y, double height, double aspect)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].x = x;
    after.items[index].y = y;
    after.items[index].height = height;
    const bool reshaped = after.items[index].aspect != aspect;
    after.items[index].aspect = aspect;
    changeOverlay(reshaped ? tr("Change a gauge's shape") : tr("Move gauge"), after);
}

void MainWindow::setGaugeChannel(int index, const QString &channel)
{
    if (index < 0 || index >= m_project.overlay.size() || channel.isEmpty())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    if (after.items[index].channel != channel)
        {
            // an end chosen for another measurement means nothing for this one
            after.items[index].scaleMax = 0.0;
        }
    after.items[index].channel = channel;
    changeOverlay(tr("Change what a gauge shows"), after);
}

void MainWindow::setGaugePartChannel(int index, const QString &own, const QString &shown)
{
    if (index < 0 || index >= m_project.overlay.size() || own.isEmpty() || shown.isEmpty())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    QHash<QString, QString> &channels = after.items[index].channels;
    if (shown == own)
        {
            channels.remove(own);
        }
    else
        {
            channels.insert(own, shown);
        }
    if (after.items != m_project.overlay)
        {
            changeOverlay(tr("Change what a gauge shows"), after);
        }
}

void MainWindow::setGaugeScaleMax(int index, double max)
{
    if (index < 0 || index >= m_project.overlay.size() || m_project.overlay.at(index).scaleMax == max)
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].scaleMax = max;
    changeOverlay(tr("Change a gauge's scale"), after);
}

void MainWindow::setGaugeStreets(int index, bool shown)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].streets = shown ? OverlayItem::Shown : OverlayItem::Hidden;
    changeOverlay(shown ? tr("Show the street map") : tr("Hide the street map"), after);
}

void MainWindow::setGaugeCloseUp(int index, double meters, const GaugeComponent::SpeedZoom &zoom)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].closeUp = meters;
    after.items[index].speedZoom = meters > 0.0 ? zoom : GaugeComponent::SpeedZoom();
    changeOverlay(zoom.isOn() ? tr("Widen the close-up with the speed")
                  : meters > 0.0 ? tr("Show the route close up") : tr("Show the whole route"), after);
}

void MainWindow::askGaugeCloseUp(int index, bool withSpeed)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    const OverlayItem &item = m_project.overlay.at(index);
    const Units units = item.unitsFor(m_units);
    const double current = m_canvas->overlay().closeUp(index);
    QDialog dialog(this);
    dialog.setWindowTitle(withSpeed ? tr("Close-up by speed") : tr("Close-up"));
    QFormLayout *form = new QFormLayout(&dialog);
    auto meters = [&dialog](double value)
    {
        QSpinBox *spin = new QSpinBox(&dialog);
        spin->setRange(50, 100000);
        spin->setSingleStep(100);
        spin->setSuffix(tr(" m"));
        spin->setValue(int(std::lround(value)));
        return spin;
    };
    auto speed = [&dialog, &units](double metersPerSecond)
    {
        QSpinBox *spin = new QSpinBox(&dialog);
        spin->setRange(0, 1000);
        spin->setSuffix(" " + units.speedLabel());
        spin->setValue(int(std::lround(metersPerSecond * units.speedScale())));
        return spin;
    };
    // what it was, else a ride's: close in town, wide on the open road
    const GaugeComponent::SpeedZoom was = item.speedZoom.isOn() ? item.speedZoom
                                          : GaugeComponent::SpeedZoom{1500.0, 10.0 / 3.6, 40.0 / 3.6};
    QSpinBox *near = meters(current > 0.0 ? current : 300.0);
    // only the popup by speed has these: made for the other, they would sit on it unplaced
    QSpinBox *slow = nullptr;
    QSpinBox *far = nullptr;
    QSpinBox *fast = nullptr;
    if (withSpeed)
        {
            slow = speed(was.slow);
            far = meters(was.far);
            fast = speed(was.fast);
            QLabel *about = new QLabel(tr("The map shows less of the route when slow and more when fast, "
                                          "in proportion between the two speeds."), &dialog);
            about->setWordWrap(true);
            form->addRow(about);
            form->addRow(tr("At this speed or slower:"), slow);
            form->addRow(tr("Shows across:"), near);
            form->addRow(tr("At this speed or faster:"), fast);
            form->addRow(tr("Shows across:"), far);
        }
    else
        {
            form->addRow(tr("Shows across:"), near);
        }
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
    GaugeComponent::SpeedZoom zoom;
    if (withSpeed)
        {
            zoom.far = far->value();
            zoom.slow = slow->value() / units.speedScale();
            zoom.fast = fast->value() / units.speedScale();
            if (!zoom.isOn())
                {
                    QMessageBox::information(this, dialog.windowTitle(), tr("The second speed has to be above the first."));
                    return;
                }
        }
    setGaugeCloseUp(index, near->value(), zoom);
}

QStringList MainWindow::channelChoices(const QString &current) const
{
    QStringList channels = m_session ? m_session->channelIds() : QStringList();
    if (!current.isEmpty() && !channels.contains(current))
        {
            // bound to something this recording lacks: still has to be shown as the choice made
            channels.prepend(current);
        }
    return channels;
}

void MainWindow::showItemMenu(int index, const QPoint &globalPosition)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    const QString current = m_project.overlay.at(index).channel;
    QMenu menu(this);
    QMenu *showMenu = menu.addMenu(tr("Show"));
    for (const QString &channel : channelChoices(current))
        {
            const Channel *data = m_session ? m_session->channel(channel) : nullptr;
            QAction *action = showMenu->addAction(ChannelFormat::displayFor(channel, data ? data->unit() : QString(), m_units).name);
            action->setCheckable(true);
            action->setChecked(channel == current);
            connect(action, &QAction::triggered, this, [this, index, channel]()
            {
                setGaugeChannel(index, channel);
            });
        }
    const Overlay &overlay = m_canvas->overlay();
    if (index < overlay.count() && overlay.hasGraph(index))
        {
            // an elevation profile, or the values as time went
            QAction *distance = menu.addAction(tr("Graph along the distance"));
            distance->setToolTip(tr("Across the distance ridden, stops taking no room; otherwise across the time"));
            distance->setCheckable(true);
            distance->setChecked(overlay.byDistance(index));
            connect(distance, &QAction::triggered, this, [this, index](bool on)
            {
                if (index < m_project.overlay.size())
                    {
                        OverlayState after{m_project.overlay, m_project.theme};
                        after.items[index].byDistance = on ? 1 : 0;
                        changeOverlay(on ? tr("Graph along the distance") : tr("Graph over the time"), after);
                    }
            });
        }
    if (index < overlay.count() && overlay.hasRoute(index))
        {
            QAction *streets = menu.addAction(tr("Street map"));
            streets->setCheckable(true);
            streets->setChecked(overlay.hasStreets(index));
            connect(streets, &QAction::triggered, this, [this, index](bool shown)
            {
                setGaugeStreets(index, shown);
            });
            // the land in 3D around the rider, the camera behind and above them
            QAction *flyover = menu.addAction(tr("3D flyover"));
            flyover->setToolTip(tr("The land in 3D, seen from behind the rider; the close-up sets how much of it"));
            flyover->setCheckable(true);
            flyover->setChecked(overlay.flyover(index));
            connect(flyover, &QAction::triggered, this, [this, index](bool on)
            {
                if (index < m_project.overlay.size())
                    {
                        OverlayState after{m_project.overlay, m_project.theme};
                        after.items[index].flyover = on ? 1 : 0;
                        changeOverlay(on ? tr("Show the route in 3D") : tr("Show the route flat"), after);
                    }
            });
            // the reference lap's rider beside this lap's
            QAction *ghost = menu.addAction(tr("Ghost of the reference lap"));
            ghost->setToolTip(tr("The rider of the reference lap too, faint, as far into their lap; not in 3D"));
            ghost->setCheckable(true);
            ghost->setChecked(overlay.ghost(index));
            ghost->setEnabled(!overlay.flyover(index));
            connect(ghost, &QAction::triggered, this, [this, index](bool on)
            {
                if (index < m_project.overlay.size())
                    {
                        OverlayState after{m_project.overlay, m_project.theme};
                        after.items[index].ghost = on ? 1 : 0;
                        changeOverlay(on ? tr("Show the ghost of the reference lap") : tr("Hide the ghost of the reference lap"), after);
                    }
            });
            QAction *camera = menu.addAction(tr("3D flyover camera..."));
            camera->setEnabled(overlay.flyover(index));
            connect(camera, &QAction::triggered, this, [this, index]() { askFlyoverCamera(index); });
            // a map of its own: satellite pictures under one gauge, streets under another
            QMenu *mapMenu = menu.addMenu(tr("Map"));
            const QString ownMap = m_project.overlay.at(index).map;
            QVector<QPair<QString, QString>> maps{{tr("As in the settings"), QString()}};
            for (const TileStore::Provider &provider : AppSettings::usableMapProviders())
                {
                    maps.append({provider.name, provider.id});
                }
            for (const auto &map : maps)
                {
                    QAction *action = mapMenu->addAction(map.first);
                    action->setCheckable(true);
                    action->setChecked(map.second == ownMap);
                    const QString id = map.second;
                    connect(action, &QAction::triggered, this, [this, index, id]()
                    {
                        if (index < m_project.overlay.size())
                            {
                                OverlayState after{m_project.overlay, m_project.theme};
                                after.items[index].map = id;
                                // a map chosen is a map shown
                                if (!id.isEmpty())
                                    {
                                        after.items[index].streets = OverlayItem::Shown;
                                    }
                                changeOverlay(tr("Choose a gauge's map"), after);
                            }
                    });
                    if (id.isEmpty())
                        {
                            mapMenu->addSeparator();
                        }
                }
            // the whole route, or this much of it around the rider
            QMenu *closeUp = menu.addMenu(tr("Close-up"));
            const double current = overlay.closeUp(index);
            const QVector<QPair<QString, double>> choices
            {
                {tr("Off: the whole route"), 0.0}, {tr("300 m"), 300.0}, {tr("600 m"), 600.0},
                {tr("1 km"), 1000.0}, {tr("2 km"), 2000.0}, {tr("5 km"), 5000.0},
            };
            const bool bySpeed = m_project.overlay.at(index).speedZoom.isOn();
            bool fixedShown = false;
            for (const auto &choice : choices)
                {
                    QAction *action = closeUp->addAction(choice.first);
                    action->setCheckable(true);
                    action->setChecked(!bySpeed && choice.second == current);
                    fixedShown = fixedShown || action->isChecked();
                    const double meters = choice.second;
                    connect(action, &QAction::triggered, this, [this, index, meters]()
                    {
                        setGaugeCloseUp(index, meters);
                    });
                }
            QAction *custom = closeUp->addAction(!bySpeed && !fixedShown && current > 0.0
                                                 ? tr("Custom: %1 m...").arg(std::lround(current)) : tr("Custom..."));
            custom->setCheckable(true);
            custom->setChecked(!bySpeed && !fixedShown && current > 0.0);
            connect(custom, &QAction::triggered, this, [this, index]() { askGaugeCloseUp(index, false); });
            QAction *dynamic = closeUp->addAction(tr("Dynamic, by speed..."));
            dynamic->setToolTip(tr("Closer when slow, wider when fast"));
            dynamic->setCheckable(true);
            dynamic->setChecked(bySpeed);
            dynamic->setEnabled(m_session && m_session->channel(ChannelId::Speed));
            connect(dynamic, &QAction::triggered, this, [this, index]() { askGaugeCloseUp(index, true); });
            closeUp->addSeparator();
            QAction *ahead = closeUp->addAction(tr("The way ahead up"));
            ahead->setToolTip(tr("Turns the close-up so that the direction of travel points up; north is up otherwise"));
            ahead->setCheckable(true);
            ahead->setChecked(overlay.headingUp(index));
            ahead->setEnabled(current > 0.0);
            connect(ahead, &QAction::triggered, this, [this, index](bool up)
            {
                if (index < m_project.overlay.size())
                    {
                        OverlayState after{m_project.overlay, m_project.theme};
                        after.items[index].headingUp = up ? 1 : 0;
                        changeOverlay(up ? tr("Turn the map to the way ahead") : tr("Turn the map north up"), after);
                    }
            });
        }
    // its own units: one gauge in miles beside the others in kilometers
    QMenu *unitsMenu = menu.addMenu(tr("Units"));
    const QVector<QPair<QString, OverlayItem::UnitSystem>> systems
    {
        {tr("As in the settings"), OverlayItem::AsSettings}, {tr("Metric (km/h, m, °C)"), OverlayItem::MetricUnits},
        {tr("Imperial (mph, ft, °F)"), OverlayItem::ImperialUnits},
    };
    for (const auto &system : systems)
        {
            QAction *action = unitsMenu->addAction(system.first);
            action->setCheckable(true);
            action->setChecked(m_project.overlay.at(index).units == system.second);
            const OverlayItem::UnitSystem units = system.second;
            connect(action, &QAction::triggered, this, [this, index, units]()
            {
                if (index < m_project.overlay.size())
                    {
                        OverlayState after{m_project.overlay, m_project.theme};
                        after.items[index].units = units;
                        changeOverlay(tr("Change a gauge's units"), after);
                    }
            });
        }
    // set free, its width and height change on their own: a profile as wide as the picture
    QAction *keepShape = menu.addAction(tr("Keep the gauge's shape"));
    keepShape->setCheckable(true);
    keepShape->setChecked(m_project.overlay.at(index).aspect <= 0.0);
    keepShape->setToolTip(tr("Off: handles on the right and bottom edges change only the width or the height. "
                             "Round parts become oval."));
    menu.setToolTipsVisible(true);
    connect(keepShape, &QAction::triggered, this, [this, index](bool keep)
    {
        if (index < m_project.overlay.size() && index < m_canvas->overlay().count())
            {
                OverlayState after{m_project.overlay, m_project.theme};
                // freed where it is, so nothing moves until a handle is dragged
                const QRectF rect = m_canvas->overlay().itemRect(index, QRectF(0, 0, 1000, 1000));
                after.items[index].aspect = keep ? 0.0 : rect.width() / rect.height();
                changeOverlay(keep ? tr("Keep a gauge's shape") : tr("Free a gauge's shape"), after);
            }
    });
    menu.addSeparator();
    menu.addAction(Icons::named("list-remove"), tr("Remove"), this, [this, index]()
    {
        removeGauge(index);
    });
    menu.exec(globalPosition);
}

namespace
{
const ThemeRole kThemeRoles[] =
{
    {QT_TRANSLATE_NOOP("MainWindow", "Text"), QT_TRANSLATE_NOOP("MainWindow", "Text and numbers"), &GaugeTheme::foreground},
    {QT_TRANSLATE_NOOP("MainWindow", "Background"), QT_TRANSLATE_NOOP("MainWindow", "Plates behind the gauges"),
     &GaugeTheme::background},
    {QT_TRANSLATE_NOOP("MainWindow", "Accent"), QT_TRANSLATE_NOOP("MainWindow", "Needles, bars and highlights"),
     &GaugeTheme::accent},
    {QT_TRANSLATE_NOOP("MainWindow", "2nd accent"), QT_TRANSLATE_NOOP("MainWindow", "Scales, units and labels"),
     &GaugeTheme::accent2},
};
}

QWidget *MainWindow::createPropertiesPanel()
{
    QWidget *panel = new QWidget(this);

    m_channelCombo = new QComboBox(panel);
    m_channelCombo->setToolTip(tr("The measurement the selected gauge shows"));
    m_channelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_channelCombo->setMinimumContentsLength(12);
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int row)
    {
        setGaugeChannel(m_canvas->selectedItem(), m_channelCombo->itemData(row).toString());
    });
    m_removeGaugeButton = new QPushButton(Icons::named("list-remove"), tr("Remove gauge"), panel);
    connect(m_removeGaugeButton, &QPushButton::clicked, this, [this]()
    {
        removeGauge(m_canvas->selectedItem());
    });

    m_partsHolder = new QWidget(panel);
    QVBoxLayout *partsLayout = new QVBoxLayout(m_partsHolder);
    partsLayout->setContentsMargins(0, 0, 0, 0);

    m_scaleMaxSpin = new QDoubleSpinBox(panel);
    m_scaleMaxSpin->setRange(0.0, 100000.0);
    m_scaleMaxSpin->setDecimals(1);
    m_scaleMaxSpin->setKeyboardTracking(false);
    m_scaleMaxSpin->setSpecialValueText(tr("From the data"));
    m_scaleMaxSpin->setToolTip(tr("Where the gauge's scale ends, in its units. "
                                  "From the data: a round number above the highest value of the recording."));
    connect(m_scaleMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double max)
    {
        setGaugeScaleMax(m_canvas->selectedItem(), max);
    });
    m_scaleMaxLabel = new QLabel(tr("Scale up to:"), panel);
    m_scaleMaxLabel->setToolTip(m_scaleMaxSpin->toolTip());

    QFormLayout *form = new QFormLayout(panel);
    QLabel *channelLabel = new QLabel(tr("Gauge data:"), panel);
    channelLabel->setToolTip(m_channelCombo->toolTip());
    form->addRow(channelLabel, m_channelCombo);
    form->addRow(m_partsHolder);
    form->addRow(m_scaleMaxLabel, m_scaleMaxSpin);
    // this gauge's own look
    m_textSizeSpin = new QSpinBox(panel);
    m_textSizeSpin->setRange(30, 300);
    m_textSizeSpin->setSingleStep(5);
    m_textSizeSpin->setSuffix(" %");
    m_textSizeSpin->setKeyboardTracking(false);
    m_textSizeSpin->setToolTip(tr("The letters and numbers of the selected gauge, larger or smaller than it was made with"));
    connect(m_textSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int percent)
    {
        changeGaugeLook(tr("Change a gauge's text size"), [percent](OverlayItem &item) { item.textScale = percent / 100.0; });
    });
    m_opacitySpin = new QSpinBox(panel);
    m_opacitySpin->setRange(5, 100);
    m_opacitySpin->setSingleStep(5);
    m_opacitySpin->setSuffix(" %");
    m_opacitySpin->setKeyboardTracking(false);
    m_opacitySpin->setToolTip(tr("How much of the selected gauge shows over the video; 100 %: all of it"));
    connect(m_opacitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int percent)
    {
        changeGaugeLook(tr("Change a gauge's opacity"), [percent](OverlayItem &item) { item.opacity = percent / 100.0; });
    });
    m_ownLookCheck = new QCheckBox(tr("Own colors and font"), panel);
    m_ownLookCheck->setToolTip(tr("On: the colors and the font below are this gauge's alone. Off: it looks like all the others."));
    connect(m_ownLookCheck, &QCheckBox::clicked, this, [this](bool own)
    {
        const GaugeTheme common = m_project.theme;
        // starting from the look of all gauges
        changeGaugeLook(own ? tr("Give a gauge its own look") : tr("Give a gauge the common look"), [own, common](OverlayItem &item)
        {
            item.ownTheme = own;
            item.theme = own ? common : GaugeTheme();
        });
    });
    form->addRow(tr("Text size:"), m_textSizeSpin);
    form->addRow(tr("Opacity:"), m_opacitySpin);
    form->addRow(QString(), m_ownLookCheck);
    form->addRow(QString(), m_removeGaugeButton);
    m_lookTitle = new QLabel(tr("All gauges"), panel);
    QFont bold = m_lookTitle->font();
    bold.setBold(true);
    m_lookTitle->setFont(bold);
    form->addRow(m_lookTitle);
    for (int role = 0; role < int(std::size(kThemeRoles)); ++role)
        {
            QPushButton *button = new QPushButton(panel);
            button->setMinimumWidth(80);
            connect(button, &QPushButton::clicked, this, [this, role]()
            {
                chooseThemeColor(role);
            });
            m_themeButtons.append(button);
            QLabel *label = new QLabel(tr(kThemeRoles[role].label) + ":", panel);
            label->setToolTip(tr(kThemeRoles[role].tip));
            button->setToolTip(tr(kThemeRoles[role].tip));
            form->addRow(label, button);
        }

    GaugeFonts::load();
    m_fontCombo = new QComboBox(panel);
    m_fontCombo->setToolTip(tr("The font of the gauges' texts; Default: as each gauge is designed"));
    m_fontCombo->addItem(tr("Default"), QString());
    m_fontCombo->addItem(tr("%1 (comes with the program)").arg(GaugeFonts::Family), QString(GaugeFonts::Family));
    m_fontCombo->insertSeparator(m_fontCombo->count());
    for (const QString &family : QFontDatabase::families())
        {
            if (family != QLatin1String(GaugeFonts::Family) && !QFontDatabase::isPrivateFamily(family))
                {
                    m_fontCombo->addItem(family, family);
                }
        }
    m_fontCombo->setMaxVisibleItems(20);
    // some font names are very long: the box stays narrow, the list opens wide enough to read them
    m_fontCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontCombo->setMinimumContentsLength(14);
    m_fontCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_fontCombo->view()->setMinimumWidth(m_fontCombo->view()->sizeHintForColumn(0) + 24);
    connect(m_fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        m_fontCombo->setToolTip(tr("The font of the gauges' texts; Default: as each gauge is designed") + "\n"
                                + m_fontCombo->currentText());
    });
    m_fontWeightCombo = new QComboBox(panel);
    m_fontWeightCombo->addItem(tr("Default", "font weight"), 0);
    m_fontWeightCombo->addItem(tr("Light"), 300);
    m_fontWeightCombo->addItem(tr("Regular"), 400);
    m_fontWeightCombo->addItem(tr("Medium"), 500);
    m_fontWeightCombo->addItem(tr("Semibold"), 600);
    m_fontWeightCombo->addItem(tr("Bold"), 700);
    m_fontWeightCombo->setToolTip(tr("One weight for every text; Default keeps each gauge's bold values and lighter labels"));
    m_fontEverywhereCheck = new QCheckBox(tr("Override own fonts"), panel);
    m_fontEverywhereCheck->setToolTip(tr("Imported gauges often name a font (Arial...); with this they use the one chosen here too"));
    auto changeFont = [this]()
    {
        const QString font = m_fontCombo->currentData().toString();
        const int weight = m_fontWeightCombo->currentData().toInt();
        const bool everywhere = m_fontEverywhereCheck->isChecked();
        changeTheme(tr("Change gauge font"), [font, weight, everywhere](GaugeTheme &theme)
        {
            theme.font = font;
            theme.fontWeight = weight;
            theme.fontEverywhere = everywhere;
        });
    };
    connect(m_fontCombo, QOverload<int>::of(&QComboBox::activated), this, changeFont);
    // typing filters the list by any part of the name; Enter or a click picks one
    m_fontCombo->setEditable(true);
    m_fontCombo->setInsertPolicy(QComboBox::NoInsert);
    m_fontCombo->completer()->setFilterMode(Qt::MatchContains);
    m_fontCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    m_fontCombo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_fontCombo->lineEdit()->setPlaceholderText(tr("Type to find a font"));
    // clicking into it selects the name, so typing replaces it
    m_fontCombo->lineEdit()->installEventFilter(new SelectAllOnFocus(m_fontCombo->lineEdit()));
    connect(m_fontCombo->lineEdit(), &QLineEdit::editingFinished, this, [this, changeFont]()
    {
        const int row = m_fontCombo->findText(m_fontCombo->currentText(), Qt::MatchFixedString);
        if (row >= 0)
            {
                m_fontCombo->setCurrentIndex(row);
                changeFont();
            }
        else
            {
                // not a font: back to the one chosen
                updateProperties();
            }
    });
    connect(m_fontWeightCombo, QOverload<int>::of(&QComboBox::activated), this, changeFont);
    connect(m_fontEverywhereCheck, &QCheckBox::clicked, this, changeFont);
    form->addRow(tr("Font:"), m_fontCombo);
    form->addRow(tr("Weight:"), m_fontWeightCombo);
    form->addRow(QString(), m_fontEverywhereCheck);
    return panel;
}

bool MainWindow::editsOwnLook() const
{
    const int index = m_canvas->selectedItem();
    return index >= 0 && index < m_project.overlay.size() && m_project.overlay.at(index).ownTheme;
}

const GaugeTheme &MainWindow::shownTheme() const
{
    return editsOwnLook() ? m_project.overlay.at(m_canvas->selectedItem()).theme : m_project.theme;
}

void MainWindow::changeTheme(const QString &description, const std::function<void(GaugeTheme &)> &change)
{
    OverlayState after{m_project.overlay, m_project.theme};
    change(editsOwnLook() ? after.items[m_canvas->selectedItem()].theme : after.theme);
    changeOverlay(description, after);
}

void MainWindow::changeGaugeLook(const QString &description, const std::function<void(OverlayItem &)> &change)
{
    const int index = m_canvas->selectedItem();
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    change(after.items[index]);
    changeOverlay(description, after);
}

void MainWindow::chooseThemeColor(int role)
{
    const QColor current = shownTheme().*(kThemeRoles[role].color);
    // transparency matters here: gauge plates are usually see-through
    const QColor chosen = QColorDialog::getColor(current, this, tr(kThemeRoles[role].label), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        {
            return;
        }
    changeTheme(tr("Change gauge colors"), [role, chosen](GaugeTheme &theme) { theme.*(kThemeRoles[role].color) = chosen; });
}

void MainWindow::updateProperties()
{
    const int index = m_canvas->selectedItem();
    const bool selected = index >= 0 && index < m_project.overlay.size();
    m_channelCombo->clear();
    m_channelCombo->setEnabled(selected);
    m_removeGaugeButton->setEnabled(selected);
    if (selected)
        {
            const QString current = m_project.overlay.at(index).channel;
            for (const QString &channel : channelChoices(current))
                {
                    const Channel *data = m_session ? m_session->channel(channel) : nullptr;
                    m_channelCombo->addItem(ChannelFormat::displayFor(channel, data ? data->unit() : QString(), m_units).name, channel);
                }
            m_channelCombo->setCurrentIndex(m_channelCombo->findData(current));
        }

    // the gauge's parts that follow channels of their own, each with its choice
    if (m_parts)
        {
            m_parts->hide();
            m_parts->deleteLater();
        }
    m_parts = new QWidget(m_partsHolder);
    m_partsHolder->layout()->addWidget(m_parts);
    QFormLayout *partsForm = new QFormLayout(m_parts);
    partsForm->setContentsMargins(0, 0, 0, 0);
    const OverlayItem item = selected ? m_project.overlay.at(index) : OverlayItem();
    const Gauge gauge = selected ? GaugeLibrary::gauge(item.gaugeId) : Gauge();
    auto nameOf = [this](const QString &channel, const Units &units)
    {
        const Channel *data = m_session ? m_session->channel(channel) : nullptr;
        return ChannelFormat::displayFor(channel, data ? data->unit() : QString(), units);
    };
    for (const QString &own : GaugeLibrary::ownChannels(gauge))
        {
            const QString shown = item.channels.value(own, own);
            QComboBox *combo = new QComboBox(m_parts);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combo->setMinimumContentsLength(12);
            QStringList choices = channelChoices(shown);
            if (!choices.contains(own))
                {
                    // the part's own channel stays a choice, even when the data lacks it
                    choices.append(own);
                }
            for (const QString &channel : choices)
                {
                    combo->addItem(nameOf(channel, m_units).name, channel);
                }
            combo->setCurrentIndex(combo->findData(shown));
            combo->setToolTip(tr("What the part of the gauge made for %1 shows").arg(nameOf(own, m_units).name));
            connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, index, own, combo](int row)
            {
                setGaugePartChannel(index, own, combo->itemData(row).toString());
            });
            partsForm->addRow(nameOf(own, m_units).name + ":", combo);
        }

    const bool scaled = selected && GaugeLibrary::hasScale(gauge);
    m_scaleMaxLabel->setVisible(scaled);
    m_scaleMaxSpin->setVisible(scaled);
    if (scaled)
        {
            const QSignalBlocker blocker(m_scaleMaxSpin);
            const QString unit = nameOf(item.channel, item.unitsFor(m_units)).unit;
            m_scaleMaxSpin->setSuffix(unit.isEmpty() ? QString() : " " + unit);
            m_scaleMaxSpin->setValue(item.scaleMax);
        }
    {
        const QSignalBlocker sizeBlocker(m_textSizeSpin);
        const QSignalBlocker opacityBlocker(m_opacitySpin);
        m_textSizeSpin->setEnabled(selected);
        m_opacitySpin->setEnabled(selected);
        m_ownLookCheck->setEnabled(selected);
        m_textSizeSpin->setValue(selected ? int(std::lround(item.textScale * 100.0)) : 100);
        m_opacitySpin->setValue(selected ? int(std::lround(item.opacity * 100.0)) : 100);
        m_ownLookCheck->setChecked(selected && item.ownTheme);
    }
    const GaugeTheme &theme = shownTheme();
    m_lookTitle->setText(editsOwnLook() ? tr("This gauge") : tr("All gauges"));
    m_lookTitle->setToolTip(editsOwnLook() ? tr("Colors and font of the selected gauge alone")
                            : tr("Colors and font of every gauge on the video"));
    for (int role = 0; role < m_themeButtons.size(); ++role)
        {
            const QColor color = theme.*(kThemeRoles[role].color);
            // a swatch; the text keeps it usable for those who cannot tell the colors apart
            QPixmap swatch(32, 16);
            swatch.fill(color);
            m_themeButtons.at(role)->setIcon(QIcon(swatch));
            m_themeButtons.at(role)->setText(color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
        }
    {
        const QSignalBlocker fontBlocker(m_fontCombo);
        const QSignalBlocker weightBlocker(m_fontWeightCombo);
        const QSignalBlocker everywhereBlocker(m_fontEverywhereCheck);
        int row = m_fontCombo->findData(theme.font);
        if (row < 0)
            {
                // a font this computer does not have: still shown as the one chosen
                m_fontCombo->addItem(theme.font, theme.font);
                row = m_fontCombo->count() - 1;
            }
        m_fontCombo->setCurrentIndex(row);
        m_fontWeightCombo->setCurrentIndex(std::max(0, m_fontWeightCombo->findData(theme.fontWeight)));
        m_fontEverywhereCheck->setChecked(theme.fontEverywhere);
        m_fontEverywhereCheck->setEnabled(!theme.font.isEmpty() || theme.fontWeight > 0);
    }
}

void MainWindow::telemetryImported(const QString &path, const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session,
                                   const QString &error)
{
    if (path != m_pendingTelemetryPath)
        {
            return;
        }
    m_pendingTelemetryPath.clear();
    statusBar()->clearMessage();
    const bool quietly = m_pendingQuietly;
    m_pendingQuietly = false;
    if (!session)
        {
            if (!quietly)
                {
                    QMessageBox::warning(this, tr("Open data"), error);
                }
            return;
        }
    m_project.telemetryPath = path;
    m_dataSourcesAction->setEnabled(true);
    m_saveDataAction->setEnabled(true);
    if (m_pendingRecentData)
        {
            m_pendingRecentData = false;
            AppSettings::addRecentFile(AppSettings::RecentData, path);
        }
    m_heldTime = -1.0;
    m_rawSession = raw;
    m_recordedSession = session;
    m_dataPanel->setSessions(raw, session, m_units);
    m_session = LapEngine::apply(session, m_project.startFinish, m_project.sectors, m_project.referenceLap);
    m_lapsPanel->setSession(m_session, m_project.startFinish, m_project.sectors);
    m_lapsPanel->setSurfaces(m_project.surfaces);
    m_timelinePanel->setSession(m_session, m_units);
    m_comparePanel->setSession(m_session, m_units);
    if (!m_pendingKeepsSync)
        {
            m_project.syncIsManual = false;
        }
    rebuildOverlay();
    updateSourcesInfo();
    autoSync();
}

void MainWindow::togglePlayback()
{
    if (!m_project.hasPicture())
        {
            return;
        }
    if (m_playback->playbackState() == QMediaPlayer::PlayingState)
        {
            m_playback->pause();
        }
    else
        {
            m_playback->play();
        }
}

void MainWindow::onPlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    m_playButton->setIcon(Icons::named(state == QMediaPlayer::PlayingState ? "media-playback-pause"
                                                                           : "media-playback-start"));
    syncMusic();
}

void MainWindow::onDurationChanged(qint64 durationMs)
{
    m_seekSlider->setRange(0, int(durationMs));
    m_seekSlider->setEnabled(durationMs > 0);
    m_timelinePanel->setVideoDuration(durationMs / 1000.0);
    m_musicPanel->setVideoDuration(durationMs / 1000.0);
    updateTimeLabel();
    // the overlap check of the clock match needs the duration
    autoSync();
}

void MainWindow::onPositionChanged(qint64 positionMs)
{
    if (!m_seekSlider->isSliderDown())
        {
            m_seekSlider->setValue(int(positionMs));
        }
    m_timelinePanel->setPosition(positionMs / 1000.0);
    m_musicPanel->setPosition(positionMs / 1000.0);
    syncMusic();
    // playing goes over what is cut, as the export will
    if (m_playback->playbackState() == QMediaPlayer::PlayingState)
        {
            if (const TimeRange *cut = CutList::cutAt(m_project.cuts, positionMs / 1000.0))
                {
                    m_playback->setPosition(qint64(std::ceil(cut->end * 1000.0)));
                }
        }
    updateTimeLabel();
}

void MainWindow::onPlayerError(QMediaPlayer::Error error, const QString &message)
{
    if (error != QMediaPlayer::NoError)
        {
            statusBar()->showMessage(tr("Video: %1").arg(message));
        }
}

void MainWindow::onOffsetEdited(double seconds)
{
    m_project.syncIsManual = true;
    m_project.syncOffset = seconds;
    setModified(true);
    m_canvas->setSync(m_project.sync());
    m_timelinePanel->setSync(m_project.sync());
    showSyncExplanation(tr("Set by hand."));
}

void MainWindow::autoSync()
{
    if (m_project.syncIsManual || m_project.videoPath.isEmpty() || !m_session)
        {
            return;
        }
    // data read out of the video itself is on the video's clock already
    if (QFileInfo(m_project.videoPath) == QFileInfo(m_project.telemetryPath))
        {
            setSyncOffset(0.0, tr("The data comes from this video: nothing to synchronize."));
            return;
        }
    const QDateTime wallClock = VideoSync::wallClockFromFileName(QFileInfo(m_project.videoPath).fileName());
    // what this computer's time zone was at that date and time
    const int systemUtcOffset = wallClock.isValid() ? QDateTime(wallClock.date(), wallClock.time()).offsetFromUtc() : 0;
    // a time-lapse covers its length times its rate of the data
    const double covered = m_playback->duration() / 1000.0 * m_project.sync().rate;
    const VideoSync::Result result = VideoSync::match(*m_session, wallClock, covered, systemUtcOffset);
    if (result.ok)
        {
            setSyncOffset(result.offsetSeconds, result.explanation);
        }
    else
        {
            setSyncOffset(0.0, tr("Not synchronized: %1 Set the offset by hand.").arg(result.explanation));
        }
}

void MainWindow::setSyncOffset(double seconds, const QString &explanation)
{
    {
        const QSignalBlocker blocker(m_offsetSpin);
        m_offsetSpin->setValue(seconds);
    }
    {
        const QSignalBlocker blocker(m_rateSpin);
        m_rateSpin->setValue(m_project.syncRate);
    }
    // without a video the data plays on its own clock
    m_rateSpin->setEnabled(!m_project.videoPath.isEmpty());
    m_project.syncOffset = seconds;
    m_canvas->setSync(m_project.sync());
    m_timelinePanel->setSync(m_project.sync());
    showSyncExplanation(explanation);
}

void MainWindow::showSyncExplanation(const QString &explanation)
{
    // what the offset is based on can be a long sentence: in the tooltips, and for a while below
    const QString offsetTip = tr("Time in the data at the first frame of the video.\n"
                                 "Increase it if the gauges are late compared to the picture.");
    const QString autoTip = tr("Set the offset from the recording time of the video and the data");
    m_offsetSpin->setToolTip(explanation.isEmpty() ? offsetTip : offsetTip + "\n\n" + explanation);
    m_autoSyncButton->setToolTip(explanation.isEmpty() ? autoTip : autoTip + "\n\n" + explanation);
    if (!explanation.isEmpty())
        {
            statusBar()->showMessage(explanation, 10000);
        }
}

void MainWindow::updateTimeLabel()
{
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(m_playback->position()), formatTime(m_playback->duration())));
}

void MainWindow::updateSourcesInfo()
{
    m_dataPanel->setCanCutVideo(!m_project.videoPath.isEmpty());
    QStringList lines;
    if (m_project.videoPath.isEmpty() && m_project.background.isOn())
        {
            const VideoBackground &background = m_project.background;
            lines << tr("Video: none, the gauges over %1 (%2 x %3, %4 fps)")
                  .arg(background.isTransparent() ? tr("nothing") : background.color.name())
                  .arg(background.size.width()).arg(background.size.height()).arg(background.fps);
        }
    else
        {
            lines << tr("Video: %1").arg(m_project.videoPath.isEmpty() ? tr("none") : QFileInfo(m_project.videoPath).fileName());
        }
    lines << QString();
    if (!m_session)
        {
            lines << tr("Data: %1").arg(tr("none"));
        }
    else
        {
            lines << tr("Data: %1").arg(QFileInfo(m_project.telemetryPath).fileName());
            for (const DataSource &source : std::as_const(m_project.moreData))
                {
                    lines << tr("  and %1, shifted by %2 s").arg(QFileInfo(source.path).fileName(),
                                                               QLocale().toString(source.shift, 'f', 1));
                }
            lines << m_session->sourceInfo();
            if (m_session->startUtc().isValid())
                {
                    lines << tr("Start: %1 UTC").arg(QLocale().toString(m_session->startUtc(), QLocale::ShortFormat));
                }
            lines << tr("Duration: %1").arg(formatTime(qint64(m_session->duration() * 1000)));
            lines << tr("Laps: %1, pauses: %2").arg(m_session->laps().size()).arg(m_session->pauses().size());
            if (m_terrainWaiting)
                {
                    lines << tr("Fetching the heights of the terrain...");
                }
            if (m_weatherWaiting)
                {
                    lines << tr("Fetching the weather of the ride...");
                }
            else if (m_weather.enabled && !m_project.wind.used && !WeatherStore::instance()->problem().isEmpty())
                {
                    lines << tr("Weather: %1").arg(WeatherStore::instance()->problem());
                }
            if (!m_session->warnings().isEmpty())
                {
                    lines << QString();
                    lines << tr("Warnings:");
                    for (const QString &warning : m_session->warnings())
                        {
                            lines << "  " + warning;
                        }
                }
        }
    m_sourcesLabel->setText(lines.join('\n'));

    m_channelTree->clear();
    if (m_session)
        {
            for (const QString &id : m_session->channelIds())
                {
                    const Channel *channel = m_session->channel(id);
                    const ChannelDisplay display = ChannelFormat::displayFor(*channel, m_units);
                    QTreeWidgetItem *item = new QTreeWidgetItem(m_channelTree);
                    item->setData(NameColumn, Qt::UserRole, id);
                    item->setText(NameColumn, display.name);
                    QString tip = tr("%1: %n samples", nullptr, channel->count()).arg(id);
                    if (m_session->derivedChannels().contains(id))
                        {
                            // not in the file: say so, it is an estimate
                            QFont font = item->font(NameColumn);
                            font.setItalic(true);
                            item->setFont(NameColumn, font);
                            tip += "\n" + tr("Not recorded: worked out from the other measurements");
                        }
                    item->setToolTip(NameColumn, tip);
                    item->setText(MinColumn, display.text(display.convert(channel->minValue())));
                    item->setText(MaxColumn, display.text(display.convert(channel->maxValue())));
                    item->setText(UnitColumn, display.unit);
                    for (int column : {NowColumn, MinColumn, MaxColumn})
                        {
                            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
                        }
                }
        }
    updateChannelValues();
}

void MainWindow::updateLaps()
{
    if (!m_recordedSession)
        {
            m_lapsPanel->setSession(nullptr, m_project.startFinish, m_project.sectors);
            m_lapsPanel->setSurfaces(m_project.surfaces);
            m_timelinePanel->setSession(nullptr, m_units);
            m_comparePanel->setSession(nullptr, m_units);
            return;
        }
    m_session = LapEngine::apply(m_recordedSession, m_project.startFinish, m_project.sectors, m_project.referenceLap);
    m_lapsPanel->setSession(m_session, m_project.startFinish, m_project.sectors);
    m_lapsPanel->setSurfaces(m_project.surfaces);
    m_timelinePanel->setSession(m_session, m_units);
    m_comparePanel->setSession(m_session, m_units);
    // laps the table kept selected (the same laps, worked out again) are compared again
    m_comparePanel->setLaps(m_lapsPanel->selectedLaps());
    rebuildOverlay();
    updateSourcesInfo();
}

StartFinishLine MainWindow::lineNear(double lat, double lon, const QString &offRoute)
{
    if (!m_recordedSession)
        {
            return StartFinishLine();
        }
    double distance = 0.0;
    const double time = LapEngine::nearestTime(*m_recordedSession, lat, lon, &distance);
    // a click well off the route is not meant for it
    if (time < 0.0 || distance > 60.0)
        {
            statusBar()->showMessage(offRoute, 5000);
            return StartFinishLine();
        }
    const StartFinishLine line = LapEngine::lineAt(*m_recordedSession, time);
    if (!line.isValid())
        {
            statusBar()->showMessage(tr("The direction of travel is not known there."), 5000);
        }
    return line;
}

void MainWindow::placeStartFinish(double lat, double lon)
{
    const StartFinishLine line = lineNear(lat, lon, tr("Click on the route to place the start/finish line."));
    if (line.isValid())
        {
            setLapLines(line, m_project.sectors, tr("Place start/finish line"));
        }
}

void MainWindow::placeSector(double lat, double lon)
{
    const StartFinishLine line = lineNear(lat, lon, tr("Click on the route to place the sector line."));
    if (line.isValid())
        {
            setLapLines(m_project.startFinish, m_project.sectors + QVector<StartFinishLine>{line}, tr("Add sector line"));
        }
}

void MainWindow::setLapLines(const StartFinishLine &line, const QVector<StartFinishLine> &sectors,
                             const QString &description)
{
    const StartFinishLine before = m_project.startFinish;
    const QVector<StartFinishLine> sectorsBefore = m_project.sectors;
    if (before == line && sectorsBefore == sectors)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, sectorsBefore, line, sectors](bool forward)
    {
        m_project.startFinish = forward ? line : before;
        m_project.sectors = forward ? sectors : sectorsBefore;
        setModified(true);
        updateLaps();
    }));
}

void MainWindow::markSurface(double fromLat, double fromLon, double toLat, double toLon, const QString &surface,
                             double factor)
{
    const Channel *distance = m_recordedSession ? m_recordedSession->channel(ChannelId::Distance) : nullptr;
    if (!distance || distance->isEmpty())
        {
            return;
        }
    double fromOff = 0.0;
    double toOff = 0.0;
    const double fromTime = LapEngine::nearestTime(*m_recordedSession, fromLat, fromLon, &fromOff);
    double toTime = LapEngine::nearestTime(*m_recordedSession, toLat, toLon, &toOff);
    // on a route that comes by the same place again, the end is the first time after the beginning
    for (const double pass : LapEngine::passes(*m_recordedSession, toLat, toLon, 40.0))
        {
            if (pass > fromTime)
                {
                    toTime = pass;
                    break;
                }
        }
    // a click well off the route is not meant for it
    if (fromTime < 0.0 || toTime < 0.0 || fromOff > 60.0 || toOff > 60.0)
        {
            statusBar()->showMessage(tr("Click on the route to mark a surface."), 5000);
            return;
        }
    SurfaceStretch stretch;
    stretch.from = distance->valueAt(std::min(fromTime, toTime));
    stretch.to = distance->valueAt(std::max(fromTime, toTime));
    stretch.surface = surface;
    stretch.factor = factor;
    if (stretch.to <= stretch.from)
        {
            return;
        }
    QVector<SurfaceStretch> surfaces = m_project.surfaces;
    surfaces.append(stretch);
    setSurfaces(surfaces, tr("Mark surface"));
}

void MainWindow::setSurfaces(const QVector<SurfaceStretch> &surfaces, const QString &description)
{
    const QVector<SurfaceStretch> before = m_project.surfaces;
    if (before == surfaces)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, surfaces](bool forward)
    {
        m_project.surfaces = forward ? surfaces : before;
        setModified(true);
        // the power estimate counts them in
        recomputeData();
    }));
}

void MainWindow::setCuts(const QVector<TimeRange> &cuts, const QString &description)
{
    const QVector<TimeRange> before = m_project.cuts;
    if (before == cuts)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, cuts](bool forward)
    {
        m_project.cuts = forward ? cuts : before;
        setModified(true);
        m_timelinePanel->setCuts(m_project.cuts);
        m_musicPanel->setCuts(m_project.cuts);
    }));
}

void MainWindow::cutVideoOfData(double start, double end)
{
    // the moments of the data where the video shows them, within it
    const SyncMap sync = m_project.sync();
    const double duration = m_playback->duration() / 1000.0;
    const TimeRange range{std::clamp(sync.videoTime(start), 0.0, duration), std::clamp(sync.videoTime(end), 0.0, duration)};
    if (range.length() <= 0.0)
        {
            statusBar()->showMessage(tr("The video does not show that part of the data."), 5000);
            return;
        }
    setCuts(CutList::added(m_project.cuts, range), tr("Cut the video"));
}

void MainWindow::saveData()
{
    if (!m_rawSession)
        {
            return;
        }
    QSettings settings;
    const QFileInfo data(m_project.telemetryPath);
    const QString startDir = settings.value("paths/savedData", data.absolutePath()).toString();
    QString filter = tr("GPX track (*.gpx)");
    const QString path = QFileDialog::getSaveFileName(this, tr("Save data"),
                         QDir(startDir).filePath(data.completeBaseName() + "_edited.gpx"),
                         tr("GPX track (*.gpx)") + ";;" + tr("CSV, every channel (*.csv)"), &filter);
    if (path.isEmpty())
        {
            return;
        }
    settings.setValue("paths/savedData", QFileInfo(path).absolutePath());
    // the chosen type when the name has no suffix of its own
    QString target = path;
    if (QFileInfo(path).suffix().isEmpty())
        {
            target += filter.contains("csv") ? ".csv" : ".gpx";
        }
    // as recorded, with the corrections: what is worked out from it is worked out again on reading
    const TelemetrySessionPtr edited = DataEdits::apply(m_rawSession, m_project.dataEdits);
    QString error;
    if (!TelemetryWriter::save(*edited, target, &error))
        {
            QMessageBox::warning(this, tr("Save data"), tr("Could not save %1: %2").arg(QFileInfo(target).fileName(), error));
            return;
        }
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(target)), 5000);
}

void MainWindow::setSoundtrack(const Soundtrack &soundtrack, const QString &description)
{
    const Soundtrack before = m_project.soundtrack;
    if (before == soundtrack)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, soundtrack](bool forward)
    {
        m_project.soundtrack = forward ? soundtrack : before;
        m_heardSoundtrack = m_project.soundtrack;
        setModified(true);
        m_musicPanel->setSoundtrack(m_project.soundtrack);
        syncMusic();
    }));
}

QString MainWindow::playersState() const
{
    auto describe = [](const QString &name, const QMediaPlayer *player, const QAudioOutput *output)
    {
        const QString file = QFileInfo(player->source().toLocalFile()).fileName();
        const QString sound = QString("%1%2 %%3").arg(player->hasAudio() ? QString() : QStringLiteral("none, "))
                              .arg(std::lround(output->volume() * 100.0))
                              .arg(output->isMuted() ? QStringLiteral(" muted") : QString());
        return QString("%1: %2, %3, %4 / %5 s, rate %6, %7%8 | sound %9, device %10")
               .arg(name, file.isEmpty() ? QStringLiteral("no file") : file, enumKey(player->playbackState()))
               .arg(player->position() / 1000.0, 0, 'f', 2).arg(player->duration() / 1000.0, 0, 'f', 1)
               .arg(player->playbackRate())
               .arg(enumKey(player->mediaStatus()),
                    player->error() != QMediaPlayer::NoError ? ", " + player->errorString() : QString(), sound,
                    output->device().isNull() ? QStringLiteral("none") : output->device().description());
    };
    return describe(QStringLiteral("Video"), m_player, m_audioOutput) + "\n"
           + describe(QStringLiteral("Music"), m_musicPlayer, m_musicOutput);
}

void MainWindow::syncMusic()
{
    // the player tells of its state while it changes files: nothing is to be done then
    if (m_musicSwitching)
        {
            return;
        }
    const Soundtrack &soundtrack = m_heardSoundtrack;
    // a player plays no louder than the file: a boost is heard in the export only
    m_audioOutput->setVolume(float(std::min(1.0, soundtrack.videoGain())));
    const double videoTime = m_playback->position() / 1000.0;
    const bool video = !m_player->source().isEmpty() || m_playback->isClock();
    const Soundtrack::Moment moment = video && soundtrack.hasMusic()
                                      ? soundtrack.at(soundtrack.musicTime(videoTime, m_project.cuts)) : Soundtrack::Moment();
    if (moment.track < 0)
        {
            // paused, not stopped: a stop takes the sound output down with it, and on Qt 6.2's
            // GStreamer it did not always come back
            if (m_musicPlayer->playbackState() == QMediaPlayer::PlayingState)
                {
                    DiagnosticLog::note("music sync", "no music here: pause");
                    m_musicPlayer->pause();
                }
            m_musicTrack = -1;
            return;
        }
    const QString path = soundtrack.tracks.at(moment.track).path;
    if (path != m_musicSource)
        {
            // The next track. Stopped first, and not asked to play while it changes files: a play()
            // given from the signals it sends meanwhile left it saying it played the new file
            // while it stood still at its start (the second track never began by itself).
            DiagnosticLog::note("music sync", QString("track %1: %2").arg(moment.track + 1).arg(QFileInfo(path).fileName()));
            m_musicSource = path;
            m_musicSwitching = true;
            m_musicPlayer->stop();
            m_musicPlayer->setSource(QUrl::fromLocalFile(path));
            m_musicSwitching = false;
        }
    m_musicOutput->setVolume(float(std::min(1.0, moment.gain * soundtrack.musicGain())));
    if (m_playback->playbackState() != QMediaPlayer::PlayingState)
        {
            if (m_musicPlayer->playbackState() == QMediaPlayer::PlayingState)
                {
                    DiagnosticLog::note("music sync", "video not playing: pause");
                    m_musicPlayer->pause();
                }
            return;
        }
    const QMediaPlayer::MediaStatus status = m_musicPlayer->mediaStatus();
    if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::LoadingMedia)
        {
            // a file shorter than it said, or one that cannot be played: silence, not again and again
            if (status != m_musicWaitedFor)
                {
                    DiagnosticLog::note("music sync", QString("waiting, the track is %1").arg(enumKey(status)));
                }
            m_musicWaitedFor = status;
            return;
        }
    m_musicWaitedFor = QMediaPlayer::NoMedia;
    const qint64 wanted = qint64(std::llround(moment.position * 1000.0));
    // Where the music should be by now if it ran on undisturbed since the last look: a seek of
    // the video moves it elsewhere, going over a cut does not (the music runs through cuts).
    const bool sameTrack = m_musicTrack == moment.track;
    const qint64 expected = m_musicWanted + (m_musicClock.isValid() ? m_musicClock.elapsed() : 0);
    const bool jumped = !sameTrack || std::abs(wanted - expected) > 500;
    m_musicTrack = moment.track;
    m_musicWanted = wanted;
    m_musicClock.start();
    if (m_musicPlayer->playbackState() != QMediaPlayer::PlayingState)
        {
            DiagnosticLog::note("music sync", QString("play from %1 s").arg(wanted / 1000.0, 0, 'f', 1));
            // playing first: Qt 6.2 does not keep a position given to a stopped player
            m_musicPlayer->play();
            m_musicPlayer->setPosition(wanted);
        }
    else if (jumped || std::abs(m_musicPlayer->position() - wanted) > 1000)
        {
            // Put in step after a jump, or when the two have drifted clearly apart. Not for less:
            // both players tell where they are only roughly, and each seek is heard.
            DiagnosticLog::note("music sync", QString("%1: at %2 s, to %3 s").arg(jumped ? "jump" : "drift")
                                .arg(m_musicPlayer->position() / 1000.0, 0, 'f', 1).arg(wanted / 1000.0, 0, 'f', 1));
            m_musicPlayer->setPosition(wanted);
        }
}

void MainWindow::setDataEdits(const QVector<DataEdit> &edits, const QString &description)
{
    const QVector<DataEdit> before = m_project.dataEdits;
    if (before == edits)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, edits](bool forward)
    {
        m_project.dataEdits = forward ? edits : before;
        setModified(true);
        recomputeData();
        m_dataPanel->setEdits(m_project.dataEdits);
    }));
}

void MainWindow::editDataSources()
{
    if (!m_mainSession)
        {
            return;
        }
    DataSourcesDialog dialog(m_project.telemetryPath, m_mainSession, m_project.moreData, m_units, this);
    if (dialog.exec() == QDialog::Accepted)
        {
            setDataSources(dialog.sources(), tr("Change the data sources"));
        }
}

void MainWindow::setDataSources(const QVector<DataSource> &sources, const QString &description)
{
    const QVector<DataSource> before = m_project.moreData;
    if (before == sources)
        {
            return;
        }
    m_undoStack->push(new OverlayCommand(description, [this, before, sources](bool forward)
    {
        m_project.moreData = forward ? sources : before;
        setModified(true);
        // read again: the channels the gauges, the laps and the edits work on change
        loadTelemetry(m_project.telemetryPath, true);
    }));
}

void MainWindow::recomputeData()
{
    if (!m_rawSession)
        {
            return;
        }
    m_sinceRecompute.start();
    m_recordedSession = ProjectData::workedOut(m_rawSession, m_project.dataEdits, m_project.elevation, m_rider, m_project.surfaces, m_weather,
                                  m_project.wind, m_drivetrain,
                                  &m_terrainWaiting, &m_weatherWaiting);
    m_dataPanel->setSessions(m_rawSession, m_recordedSession, m_units);
    updateLaps();
}

void MainWindow::seekTelemetryTime(double time)
{
    // telemetry time = video time + offset
    const double videoTime = m_project.sync().videoTime(time);
    const double videoEnd = m_playback->duration() / 1000.0;
    const qint64 position = std::clamp<qint64>(qint64(std::llround(videoTime * 1000.0)), 0, std::max<qint64>(0, m_playback->duration()));
    if (m_project.hasPicture())
        {
            m_playback->setPosition(position);
        }
    // a moment the video has no picture for is still shown in the data's own panels
    const bool outside = !m_project.hasPicture() || videoTime < 0.0 || (videoEnd > 0.0 && videoTime > videoEnd);
    m_heldTime = outside ? time : -1.0;
    m_heldVideoPosition = outside ? (!m_project.hasPicture() ? -1 : position) : -1;
    m_dataPanel->setTime(shownTelemetryTime());
    m_lapsPanel->setTime(shownTelemetryTime());
}

double MainWindow::shownTelemetryTime() const
{
    if (m_heldTime >= 0.0)
        {
            // held until the video is played or moved
            if (!m_project.hasPicture() || std::llabs(m_playback->position() - m_heldVideoPosition) < 50)
                {
                    return m_heldTime;
                }
        }
    return m_canvas->telemetryTime();
}

void MainWindow::updateChannelValues()
{
    if (!m_session || !m_channelTree->isVisible())
        {
            return;
        }
    const double time = m_canvas->telemetryTime();
    for (int row = 0; row < m_channelTree->topLevelItemCount(); ++row)
        {
            QTreeWidgetItem *item = m_channelTree->topLevelItem(row);
            const Channel *channel = m_session->channel(item->data(NameColumn, Qt::UserRole).toString());
            bool ok = false;
            const double value = channel ? channel->valueAt(time, &ok) : 0.0;
            const ChannelDisplay display = channel ? ChannelFormat::displayFor(*channel, m_units) : ChannelDisplay();
            // outside the recording there is no value, not the last one
            const QString text = ok ? display.text(display.convert(value)) : QString("--");
            if (item->text(NowColumn) != text)
                {
                    item->setText(NowColumn, text);
                }
        }
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        {
            applySettings();
        }
}

void MainWindow::applySettings()
{
    Appearance::apply();
    applyMapSettings();
    const PowerEstimate::Rider rider = AppSettings::powerRider();
    const WeatherChoice weather = AppSettings::weather();
    const Drivetrain drivetrain = AppSettings::drivetrain();
    if (rider != m_rider || weather != m_weather || drivetrain != m_drivetrain)
        {
            m_rider = rider;
            m_weather = weather;
            m_drivetrain = drivetrain;
            recomputeData();
        }
    const Units units = AppSettings::units();
    if (units != m_units)
        {
            m_units = units;
            showWind();
            m_lapsPanel->setUnits(m_units);
            m_timelinePanel->setSession(m_session, m_units);
            m_comparePanel->setSession(m_session, m_units);
            // the same laps, in the new units: the table still shows them selected
            m_comparePanel->setLaps(m_lapsPanel->selectedLaps());
            m_dataPanel->setSessions(m_rawSession, m_recordedSession, m_units);
            rebuildOverlay();
            updateSourcesInfo();
        }
}

void MainWindow::applyMapSettings()
{
    TileStore *maps = TileStore::instance();
    const QString before = maps->urlTemplate() + maps->attribution();
    AppSettings::applyMapSources();
    // asked for only when the Weather page of the settings says so
    WeatherStore::instance()->setDownloads(true);
    m_lapsPanel->refreshMapChoice();
    if (maps->urlTemplate() + maps->attribution() != before)
        {
            rebuildOverlay();
        }
}

void MainWindow::checkForUpdates(bool asked)
{
    if (ReleaseCheck::currentTag().isEmpty())
        {
            if (asked)
                {
                    QMessageBox::information(this, tr("Check for updates"),
                                             tr("This copy was not built for a release (it is a development build or a "
                                                "distribution's package), so it cannot tell how new it is. The releases are "
                                                "<a href=\"%1\">here</a>.").arg(ReleaseCheck::releasesPage().toString()));
                }
            return;
        }
    m_updateAsked = asked;
    AppSettings::setLastUpdateCheck(QDateTime::currentDateTime());
    m_releaseCheck->check();
}

void MainWindow::showNewRelease()
{
    if (!m_newRelease.isValid())
        {
            return;
        }
    const ReleaseCheck::Release release = m_newRelease;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Version %1").arg(release.version));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QVersionNumber ours;
    QString ourBuild;
    ReleaseCheck::parseTag(ReleaseCheck::currentTag(), &ours, &ourBuild);
    const auto day = [](const QString &built) { return QDateTime::fromString(built, "yyyyMMddHHmm").date(); };
    QLabel *intro = new QLabel(tr("Version %1 of %2 is out, made on %3. This one is %4, of %5.")
                               .arg(release.version, qApp->applicationDisplayName(),
                                    QLocale().toString(day(release.built), QLocale::ShortFormat), ours.toString(),
                                    QLocale().toString(day(ourBuild), QLocale::ShortFormat)), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QTextBrowser *notes = new QTextBrowser(&dialog);
    notes->setOpenExternalLinks(true);
    notes->setMarkdown(release.notes.isEmpty() ? tr("No notes came with it.") : release.notes);
    layout->addWidget(notes, 1);
    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    // an AppImage and an installed copy replace themselves; the others are pointed to the page
    if (Updater::assetFor(release))
        {
            QPushButton *update = buttons->addButton(tr("Update now"), QDialogButtonBox::AcceptRole);
            update->setIcon(Icons::named("go-next"));
            update->setDefault(true);
            connect(update, &QPushButton::clicked, &dialog, [this, &dialog, release]()
            {
                dialog.accept();
                updateTo(release);
            });
        }
    QPushButton *page = buttons->addButton(tr("Open the release page"), QDialogButtonBox::AcceptRole);
    if (!Updater::assetFor(release))
        {
            page->setIcon(Icons::named("go-next"));
        }
    QPushButton *skip = buttons->addButton(tr("Skip this version"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    connect(page, &QPushButton::clicked, &dialog, [&dialog, release]()
    {
        QDesktopServices::openUrl(release.page.isValid() ? release.page : ReleaseCheck::releasesPage());
        dialog.accept();
    });
    connect(skip, &QPushButton::clicked, &dialog, [this, &dialog, release]()
    {
        // not shown again until a newer one comes
        AppSettings::setSkippedRelease(release.tag);
        m_updateLabel->hide();
        m_updateAction->setVisible(false);
        dialog.reject();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(560, 440);
    dialog.exec();
}

void MainWindow::updateTo(const ReleaseCheck::Release &release)
{
    const Updater::Next next = Updater::prepare(this, release);
    if (!next.isValid())
        {
            return;
        }
    if (Updater::kind() == Updater::AppImage)
        {
            m_updateLabel->hide();
            m_updateAction->setVisible(false);
            if (QMessageBox::question(this, tr("Update"), tr("Version %1 is in place. Restart now to use it?").arg(release.version))
                    != QMessageBox::Yes)
                {
                    statusBar()->showMessage(tr("Version %1 starts the next time the program does.").arg(release.version), 8000);
                    return;
                }
        }
    // the installer waits for the program to end; unsaved work is asked about first
    if (close())
        {
            QProcess::startDetached(next.program, next.arguments);
        }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About"),
                       tr("<b>%1</b><br>Version %2<br><br>"
                          "Telemetry overlays for action camera video.<br><br>"
                          "Author: coozoo<br>"
                          "<a href=\"%3\">%3</a><br>"
                          "Licensed under the GNU GPL v3.<br><br>"
                          "<img src=\":/images/icons/flag-ukraine.svg\" width=\"24\" height=\"16\"> "
                          "<a href=\"%4\">Support Ukraine</a> in its fight for independence against the empire.")
                       .arg(qApp->property("appname").toString(), qApp->property("appversion").toString(),
                            QString::fromLatin1(kRepository), QString::fromLatin1(kSupportUkraine)));
}
