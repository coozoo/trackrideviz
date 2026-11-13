#include "mainwindow.h"

#include "datapanel.h"
#include "joindialog.h"
#include "lapcomparepanel.h"
#include "lapspanel.h"
#include "timelinepanel.h"

#include "appearance.h"
#include "exportdialog.h"
#include "gauge/gaugelibrary.h"
#include "gaugelibrarypanel.h"
#include "overlay/templatelibrary.h"
#include "settings/appsettings.h"
#include "settingsdialog.h"
#include "sync/videosync.h"
#include "telemetry/dataedits.h"
#include "telemetry/derivedchannels.h"
#include "telemetry/telemetryimporter.h"
#include "videocanvas.h"

#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QtConcurrent>

namespace
{
struct ImportResult
{
    TelemetrySessionPtr raw;       // as the file gave it
    TelemetrySessionPtr session;   // corrected, and completed with what could be worked out
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
    resize(1280, 800);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoSink(m_videoSink);

    m_undoStack = new QUndoStack(this);
    setCentralWidget(createCentralWidget());
    createDocks();
    createMenus();
    statusBar();

    m_units = AppSettings::units();
    rebuildOverlay();

    connect(m_canvas, &VideoCanvas::itemGeometryEdited, this, &MainWindow::onItemGeometryEdited);
    connect(m_canvas, &VideoCanvas::itemDeleteRequested, this, &MainWindow::removeGauge);
    connect(m_canvas, &VideoCanvas::itemMenuRequested, this, &MainWindow::showItemMenu);
    connect(m_canvas, &VideoCanvas::selectedItemChanged, this, &MainWindow::updateProperties);
    connect(m_libraryPanel, &GaugeLibraryPanel::addRequested, this, &MainWindow::addGauge);
    connect(m_libraryPanel, &GaugeLibraryPanel::applyTemplateRequested, this, &MainWindow::applyTemplate);
    connect(m_libraryPanel, &GaugeLibraryPanel::libraryChanged, this, &MainWindow::rebuildOverlay);
    updateProperties();

    connect(m_videoSink, &QVideoSink::videoFrameChanged, m_canvas, &VideoCanvas::setFrame);
    connect(m_player, &QMediaPlayer::positionChanged, m_canvas, &VideoCanvas::setPosition);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);

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
                m_dataPanel->setTime(m_canvas->telemetryTime());
            }
        if (m_lapsDock->isVisible())
            {
                m_lapsPanel->setTime(m_canvas->telemetryTime());
            }
    });
    valuesTimer->start();

    updateSourcesInfo();
    updateTimeLabel();
    updateWindowTitle();

    if (AppSettings::saveWindowLayout())
        {
            QSettings settings;
            restoreGeometry(settings.value("mainwindow/geometry").toByteArray());
            restoreState(settings.value("mainwindow/state").toByteArray());
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
            settings.setValue("mainwindow/state", saveState());
        }
    QMainWindow::closeEvent(event);
}

QWidget *MainWindow::createCentralWidget()
{
    QWidget *central = new QWidget(this);
    m_canvas = new VideoCanvas(central);

    m_playButton = new QToolButton(central);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
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
        m_player->setPosition(m_seekSlider->sliderPosition());
    });

    m_timeLabel = new QLabel(central);

    m_offsetSpin = new QDoubleSpinBox(central);
    m_offsetSpin->setRange(-1e7, 1e7);
    m_offsetSpin->setDecimals(2);
    m_offsetSpin->setSingleStep(0.1);
    m_offsetSpin->setSuffix(tr(" s"));
    m_offsetSpin->setKeyboardTracking(false);
    m_offsetSpin->setToolTip(tr("Time in the data at the first frame of the video.\n"
                                "Increase it if the gauges are late compared to the picture."));
    connect(m_offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onOffsetEdited);

    m_autoSyncButton = new QToolButton(central);
    m_autoSyncButton->setText(tr("Auto"));
    m_autoSyncButton->setToolTip(tr("Set the offset from the recording time of the video and the data"));
    connect(m_autoSyncButton, &QToolButton::clicked, this, [this]()
    {
        m_project.syncIsManual = false;
        autoSync();
    });

    m_syncLabel = new QLabel(central);
    m_syncLabel->setTextFormat(Qt::PlainText);

    QHBoxLayout *transport = new QHBoxLayout;
    transport->addWidget(m_playButton);
    transport->addWidget(m_seekSlider, 1);
    transport->addWidget(m_timeLabel);

    QHBoxLayout *sync = new QHBoxLayout;
    sync->addWidget(new QLabel(tr("Data offset:"), central));
    sync->addWidget(m_offsetSpin);
    sync->addWidget(m_autoSyncButton);
    sync->addWidget(m_syncLabel, 1);

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
        m_player->setPosition(qint64(std::llround(seconds * 1000.0)));
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
    connect(m_dataPanel, &DataPanel::editsChanged, this, &MainWindow::setDataEdits);
    m_lapsPanel = new LapsPanel(m_lapsDock);
    m_lapsDock->setWidget(m_lapsPanel);
    connect(m_lapsPanel, &LapsPanel::lineRequested, this, &MainWindow::placeStartFinish);
    connect(m_lapsPanel, &LapsPanel::removeLineRequested, this, [this]()
    {
        setLapLines(StartFinishLine(), m_project.sectors, tr("Remove start/finish line"));
    });
    connect(m_lapsPanel, &LapsPanel::sectorRequested, this, &MainWindow::placeSector);
    connect(m_lapsPanel, &LapsPanel::removeSectorsRequested, this, [this]()
    {
        setLapLines(m_project.startFinish, {}, tr("Remove sector lines"));
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
    m_propertiesDock->setWidget(createPropertiesPanel());

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

    QVBoxLayout *sourcesLayout = new QVBoxLayout(sources);
    sourcesLayout->setContentsMargins(6, 6, 6, 6);
    sourcesLayout->addWidget(m_sourcesLabel);
    sourcesLayout->addWidget(m_channelTree, 1);
    m_sourcesDock->setWidget(sources);
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
    QAction *openDataAction = fileMenu->addAction(tr("Open &data..."), this, &MainWindow::chooseTelemetry);
    openDataAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    fileMenu->addAction(tr("&Join videos..."), this, &MainWindow::joinVideos);
    fileMenu->addSeparator();
    QAction *openProjectAction = fileMenu->addAction(tr("Open &project..."), this, &MainWindow::chooseProject);
    openProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    QAction *saveAction = fileMenu->addAction(tr("&Save project"), this, &MainWindow::saveProject);
    saveAction->setShortcut(QKeySequence::Save);
    QAction *saveAsAction = fileMenu->addAction(tr("Save project &as..."), this, &MainWindow::saveProjectAs);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    m_exportAction = fileMenu->addAction(tr("&Export video..."), this, &MainWindow::exportVideo);
    m_exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_exportAction->setEnabled(false);
    fileMenu->addSeparator();
    QAction *settingsAction = fileMenu->addAction(tr("&Settings..."), this, &MainWindow::showSettings);
    settingsAction->setShortcut(QKeySequence::Preferences);
    // macOS moves it to the application menu, where it is expected there
    settingsAction->setMenuRole(QAction::PreferencesRole);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_undoStack->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    QAction *redoAction = m_undoStack->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_sourcesDock->toggleViewAction());
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_timelineDock->toggleViewAction());
    viewMenu->addAction(m_compareDock->toggleViewAction());
    viewMenu->addAction(m_dataDock->toggleViewAction());

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);
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
            openVideo(path);
        }
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
    // what was cut and where breaks were belong to the video before
    m_project.videoShifts = shifts;
    m_project.cuts.clear();
    m_timelinePanel->setCuts(m_project.cuts);
    loadVideo(path);
}

void MainWindow::joinVideos()
{
    JoinDialog dialog(QStringList(), this);
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
    m_player->setSource(QUrl::fromLocalFile(path));
    // shows the first frame without starting to play
    m_player->pause();
    m_playButton->setEnabled(true);
    m_exportAction->setEnabled(true);
    updateSourcesInfo();
    autoSync();
}

void MainWindow::openTelemetry(const QString &path)
{
    // corrections belong to the data before
    m_project.dataEdits.clear();
    m_dataPanel->setEdits(m_project.dataEdits);
    loadTelemetry(path, false);
}

void MainWindow::loadTelemetry(const QString &path, bool keepSync)
{
    m_pendingTelemetryPath = path;
    m_pendingKeepsSync = keepSync;
    statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(path).fileName()));

    auto *watcher = new QFutureWatcher<ImportResult>(this);
    connect(watcher, &QFutureWatcher<ImportResult>::finished, this, [this, watcher, path]()
    {
        const ImportResult result = watcher->result();
        watcher->deleteLater();
        telemetryImported(path, result.raw, result.session, result.error);
    });
    const QVector<DataEdit> edits = m_project.dataEdits;
    watcher->setFuture(QtConcurrent::run([path, edits]()
    {
        ImportResult result;
        result.raw = TelemetryImporter::loadRecorded(path, &result.error);
        result.session = DerivedChannels::complete(DataEdits::apply(result.raw, edits));
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

    m_project = project;
    m_projectPath = path;
    m_undoStack->clear();
    setModified(false);
    m_session.reset();
    m_recordedSession.reset();
    m_rawSession.reset();
    m_dataPanel->setSessions(nullptr, nullptr, m_units);
    m_lapsPanel->setSession(nullptr, m_project.startFinish, m_project.sectors);
    m_timelinePanel->setSession(nullptr, m_units);
    m_comparePanel->setSession(nullptr, m_units);
    m_timelinePanel->setCuts(m_project.cuts);
    m_dataPanel->setEdits(m_project.dataEdits);
    m_pendingTelemetryPath.clear();
    m_canvas->clear();
    m_player->setSource(QUrl());
    m_playButton->setEnabled(false);
    m_exportAction->setEnabled(false);
    rebuildOverlay();
    setSyncOffset(m_project.syncOffset, m_project.syncIsManual ? tr("Set by hand.") : QString());
    if (!m_project.videoPath.isEmpty())
        {
            loadVideo(m_project.videoPath);
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
    return true;
}

void MainWindow::exportVideo()
{
    if (m_project.videoPath.isEmpty())
        {
            return;
        }
    if (!m_session)
        {
            QMessageBox::information(this, tr("Export video"),
                                     tr("Open the data for this video first: without it there is nothing to draw."));
            return;
        }
    // two decoders on one file only fight for the disk and the graphics card
    m_player->pause();
    ExportDialog dialog(m_project.videoPath, Overlay(m_project.overlay, m_session, m_units, m_project.theme),
                        m_project.sync(), m_project.cuts, m_player->position() / 1000.0, this);
    dialog.exec();
}

void MainWindow::rebuildOverlay()
{
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

void MainWindow::addGauge(const QString &gaugeId)
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
    item.height = 0.25;
    item.x = 0.35 + step;
    item.y = 0.3 + step;

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
    m_canvas->setSelectedItem(-1);
    changeOverlay(tr("Apply template"), {t.overlayItems(aspect), t.theme});
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

void MainWindow::onItemGeometryEdited(int index, double x, double y, double height)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].x = x;
    after.items[index].y = y;
    after.items[index].height = height;
    changeOverlay(tr("Move gauge"), after);
}

void MainWindow::setGaugeChannel(int index, const QString &channel)
{
    if (index < 0 || index >= m_project.overlay.size() || channel.isEmpty())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].channel = channel;
    changeOverlay(tr("Change what a gauge shows"), after);
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
    menu.addSeparator();
    menu.addAction(tr("Remove"), this, [this, index]()
    {
        removeGauge(index);
    });
    menu.exec(globalPosition);
}

namespace
{
const ThemeRole kThemeRoles[] =
{
    {QT_TRANSLATE_NOOP("MainWindow", "Text and numbers"), &GaugeTheme::foreground},
    {QT_TRANSLATE_NOOP("MainWindow", "Background"), &GaugeTheme::background},
    {QT_TRANSLATE_NOOP("MainWindow", "Accent"), &GaugeTheme::accent},
    {QT_TRANSLATE_NOOP("MainWindow", "Second accent"), &GaugeTheme::accent2},
};
}

QWidget *MainWindow::createPropertiesPanel()
{
    QWidget *panel = new QWidget(this);

    m_channelCombo = new QComboBox(panel);
    m_channelCombo->setToolTip(tr("The measurement the selected gauge shows"));
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int row)
    {
        setGaugeChannel(m_canvas->selectedItem(), m_channelCombo->itemData(row).toString());
    });
    m_removeGaugeButton = new QPushButton(tr("Remove gauge"), panel);
    connect(m_removeGaugeButton, &QPushButton::clicked, this, [this]()
    {
        removeGauge(m_canvas->selectedItem());
    });

    QFormLayout *form = new QFormLayout(panel);
    form->addRow(tr("Selected gauge shows:"), m_channelCombo);
    form->addRow(QString(), m_removeGaugeButton);
    QLabel *coloursTitle = new QLabel(tr("Colours of all gauges"), panel);
    QFont bold = coloursTitle->font();
    bold.setBold(true);
    coloursTitle->setFont(bold);
    form->addRow(coloursTitle);
    for (int role = 0; role < int(std::size(kThemeRoles)); ++role)
        {
            QPushButton *button = new QPushButton(panel);
            button->setMinimumWidth(80);
            connect(button, &QPushButton::clicked, this, [this, role]()
            {
                chooseThemeColor(role);
            });
            m_themeButtons.append(button);
            form->addRow(tr(kThemeRoles[role].label) + ":", button);
        }
    return panel;
}

void MainWindow::chooseThemeColor(int role)
{
    const QColor current = m_project.theme.*(kThemeRoles[role].color);
    // transparency matters here: gauge plates are usually see-through
    const QColor chosen = QColorDialog::getColor(current, this, tr(kThemeRoles[role].label), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.theme.*(kThemeRoles[role].color) = chosen;
    changeOverlay(tr("Change gauge colours"), after);
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
    for (int role = 0; role < m_themeButtons.size(); ++role)
        {
            const QColor color = m_project.theme.*(kThemeRoles[role].color);
            // a swatch; the text keeps it usable for those who cannot tell the colours apart
            QPixmap swatch(32, 16);
            swatch.fill(color);
            m_themeButtons.at(role)->setIcon(QIcon(swatch));
            m_themeButtons.at(role)->setText(color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
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
    if (!session)
        {
            QMessageBox::warning(this, tr("Open data"), error);
            return;
        }
    m_project.telemetryPath = path;
    m_rawSession = raw;
    m_recordedSession = session;
    m_dataPanel->setSessions(raw, session, m_units);
    m_session = LapEngine::apply(session, m_project.startFinish, m_project.sectors);
    m_lapsPanel->setSession(m_session, m_project.startFinish, m_project.sectors);
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
    if (m_project.videoPath.isEmpty())
        {
            return;
        }
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        {
            m_player->pause();
        }
    else
        {
            m_player->play();
        }
}

void MainWindow::onPlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    m_playButton->setIcon(style()->standardIcon(state == QMediaPlayer::PlayingState
                          ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void MainWindow::onDurationChanged(qint64 durationMs)
{
    m_seekSlider->setRange(0, int(durationMs));
    m_seekSlider->setEnabled(durationMs > 0);
    m_timelinePanel->setVideoDuration(durationMs / 1000.0);
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
    // playing goes over what is cut, as the export will
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        {
            if (const TimeRange *cut = CutList::cutAt(m_project.cuts, positionMs / 1000.0))
                {
                    m_player->setPosition(qint64(std::ceil(cut->end * 1000.0)));
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
    m_syncLabel->setText(tr("Set by hand."));
}

void MainWindow::autoSync()
{
    if (m_project.syncIsManual || m_project.videoPath.isEmpty() || !m_session)
        {
            return;
        }
    const QDateTime wallClock = VideoSync::wallClockFromFileName(QFileInfo(m_project.videoPath).fileName());
    // what this computer's time zone was at that date and time
    const int systemUtcOffset = wallClock.isValid() ? QDateTime(wallClock.date(), wallClock.time()).offsetFromUtc() : 0;
    const VideoSync::Result result = VideoSync::match(*m_session, wallClock, m_player->duration() / 1000.0, systemUtcOffset);
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
    m_project.syncOffset = seconds;
    m_canvas->setSync(m_project.sync());
    m_timelinePanel->setSync(m_project.sync());
    m_syncLabel->setText(explanation);
}

void MainWindow::updateTimeLabel()
{
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(m_player->position()), formatTime(m_player->duration())));
}

void MainWindow::updateSourcesInfo()
{
    QStringList lines;
    lines << tr("Video: %1").arg(m_project.videoPath.isEmpty() ? tr("none") : QFileInfo(m_project.videoPath).fileName());
    lines << QString();
    if (!m_session)
        {
            lines << tr("Data: %1").arg(tr("none"));
        }
    else
        {
            lines << tr("Data: %1").arg(QFileInfo(m_project.telemetryPath).fileName());
            lines << m_session->sourceInfo();
            if (m_session->startUtc().isValid())
                {
                    lines << tr("Start: %1 UTC").arg(QLocale().toString(m_session->startUtc(), QLocale::ShortFormat));
                }
            lines << tr("Duration: %1").arg(formatTime(qint64(m_session->duration() * 1000)));
            lines << tr("Laps: %1, pauses: %2").arg(m_session->laps().size()).arg(m_session->pauses().size());
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
                    if (m_session->derivedChannels().contains(id))
                        {
                            // not in the file: say so, it is an estimate
                            QFont font = item->font(NameColumn);
                            font.setItalic(true);
                            item->setFont(NameColumn, font);
                            item->setToolTip(NameColumn, tr("Not recorded: worked out from the other measurements"));
                        }
                    item->setText(MinColumn, QLocale().toString(display.convert(channel->minValue()), 'f', display.decimals));
                    item->setText(MaxColumn, QLocale().toString(display.convert(channel->maxValue()), 'f', display.decimals));
                    item->setText(UnitColumn, display.unit);
                    for (int column : {NowColumn, MinColumn, MaxColumn})
                        {
                            item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
                        }
                    item->setToolTip(NameColumn, tr("%1: %n samples", nullptr, channel->count()).arg(id));
                }
        }
    updateChannelValues();
}

void MainWindow::updateLaps()
{
    if (!m_recordedSession)
        {
            m_lapsPanel->setSession(nullptr, m_project.startFinish, m_project.sectors);
            m_timelinePanel->setSession(nullptr, m_units);
            m_comparePanel->setSession(nullptr, m_units);
            return;
        }
    m_session = LapEngine::apply(m_recordedSession, m_project.startFinish, m_project.sectors);
    m_lapsPanel->setSession(m_session, m_project.startFinish, m_project.sectors);
    m_timelinePanel->setSession(m_session, m_units);
    m_comparePanel->setSession(m_session, m_units);
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
    }));
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
        if (m_rawSession)
            {
                m_recordedSession = DerivedChannels::complete(DataEdits::apply(m_rawSession, m_project.dataEdits));
                m_dataPanel->setSessions(m_rawSession, m_recordedSession, m_units);
                updateLaps();
            }
        m_dataPanel->setEdits(m_project.dataEdits);
    }));
}

void MainWindow::seekTelemetryTime(double time)
{
    if (m_project.videoPath.isEmpty())
        {
            return;
        }
    // telemetry time = video time + offset
    m_player->setPosition(std::max<qint64>(0, qint64(std::llround(m_project.sync().videoTime(time) * 1000.0))));
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
            const QString text = ok ? QLocale().toString(display.convert(value), 'f', display.decimals) : QString("--");
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
    const Units units = AppSettings::units();
    if (units != m_units)
        {
            m_units = units;
            m_timelinePanel->setSession(m_session, m_units);
            m_comparePanel->setSession(m_session, m_units);
            m_dataPanel->setSessions(m_rawSession, m_recordedSession, m_units);
            rebuildOverlay();
            updateSourcesInfo();
        }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About"),
                       tr("<b>%1</b><br>Version %2<br><br>"
                          "Telemetry overlays for action camera video.<br>"
                          "Licensed under the GNU GPL v3.")
                       .arg(qApp->property("appname").toString(), qApp->property("appversion").toString()));
}
