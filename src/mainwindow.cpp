#include "mainwindow.h"

#include "appearance.h"
#include "exportdialog.h"
#include "settings/appsettings.h"
#include "settingsdialog.h"
#include "sync/videosync.h"
#include "telemetry/telemetryimporter.h"
#include "videocanvas.h"

#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QtConcurrent>

namespace
{
struct ImportResult
{
    TelemetrySessionPtr session;
    QString error;
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

    setCentralWidget(createCentralWidget());
    createDocks();
    createMenus();
    statusBar();

    m_units = AppSettings::units();
    rebuildOverlay();

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
    tabifyDockWidget(m_sourcesDock, m_libraryDock);
    m_sourcesDock->raise();

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

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_sourcesDock->toggleViewAction());
    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_timelineDock->toggleViewAction());

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

void MainWindow::openVideo(const QString &path)
{
    m_project.syncIsManual = false;
    loadVideo(path);
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
        telemetryImported(path, result.session, result.error);
    });
    watcher->setFuture(QtConcurrent::run([path]()
    {
        ImportResult result;
        result.session = TelemetryImporter::load(path, &result.error);
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
    m_session.reset();
    m_pendingTelemetryPath.clear();
    m_canvas->setSession(nullptr);
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
    ExportDialog dialog(m_project.videoPath, Overlay(m_project.overlay, m_units), m_session,
                        m_project.syncOffset, m_player->position() / 1000.0, this);
    dialog.exec();
}

void MainWindow::rebuildOverlay()
{
    m_canvas->setOverlay(Overlay(m_project.overlay, m_units));
}

void MainWindow::updateWindowTitle()
{
    const QString appName = qApp->property("appname").toString();
    setWindowTitle(m_projectPath.isEmpty() ? appName
                   : QString("%1 - %2").arg(QFileInfo(m_projectPath).completeBaseName(), appName));
}

void MainWindow::telemetryImported(const QString &path, const TelemetrySessionPtr &session, const QString &error)
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
    m_session = session;
    if (!m_pendingKeepsSync)
        {
            m_project.syncIsManual = false;
        }
    m_canvas->setSession(session);
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
    m_canvas->setSyncOffset(seconds);
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
    m_canvas->setSyncOffset(seconds);
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
