#include "mainwindow.h"

#include "datapanel.h"
#include "joindialog.h"
#include "lapcomparepanel.h"
#include "lapspanel.h"
#include "musicpanel.h"
#include "timelinepanel.h"

#include "appearance.h"
#include "exportdialog.h"
#include "gauge/gaugefonts.h"
#include "gauge/gaugelibrary.h"
#include "gaugelibrarypanel.h"
#include "maps/terrain.h"
#include "maps/tilestore.h"
#include "overlay/templatelibrary.h"
#include "settings/appsettings.h"
#include "settingsdialog.h"
#include "sync/videosync.h"
#include "telemetry/dataedits.h"
#include "telemetry/derivedchannels.h"
#include "telemetry/gears.h"
#include "weather/weather.h"
#include "telemetry/telemetryimporter.h"
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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
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
// A line of text that never makes the window wider: what does not fit ends in "...", and the
// whole of it is the tooltip.
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget *parent)
        : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void setFullText(const QString &text)
    {
        m_full = text;
        setToolTip(text);
        elide();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        elide();
    }

private:
    void elide()
    {
        setText(fontMetrics().elidedText(m_full, Qt::ElideRight, std::max(0, width())));
    }

    QString m_full;
};

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
    TelemetrySessionPtr raw;       // as the file gave it
    TelemetrySessionPtr session;   // corrected, and completed with what could be worked out
    bool waiting{false};           // for heights of the terrain still being fetched
    bool weatherWaiting{false};    // for the weather of the ride
    QString error;
};

// What gauges read: the file's data corrected, with the terrain's heights where the project
// asks for them, and everything that can be worked out from that.
TelemetrySessionPtr workedOut(const TelemetrySessionPtr &raw, const QVector<DataEdit> &edits, Terrain::Use elevation,
                              const PowerEstimate::Rider &rider, const QVector<SurfaceStretch> &surfaces,
                              const WeatherChoice &weatherChoice, const HandWind &handWind, const Drivetrain &drivetrain,
                              bool *waiting, bool *weatherWaiting)
{
    // the gears want the speed and the cadence only
    const TelemetrySessionPtr complete = Gears::apply(DerivedChannels::complete(Terrain::apply(DataEdits::apply(raw, edits),
                                         elevation, waiting)), drivetrain);
    *weatherWaiting = false;
    // a wind entered by hand stands in for the weather history's
    const Weather weather = complete && weatherChoice.enabled && !handWind.used
                            ? WeatherStore::instance()->along(*complete, weatherChoice, weatherWaiting) : Weather();
    const TelemetrySessionPtr windy = handWind.used ? WeatherChannels::addWind(complete, handWind, weatherChoice.windShare)
                                      : WeatherChannels::add(complete, weather, weatherChoice.windShare);
    // the estimate wants the speed, the grade and the wind; what is worked out from a power wants the estimate
    const TelemetrySessionPtr powered = PowerEstimate::apply(windy, rider, surfaces);
    return powered == complete ? complete : DerivedChannels::complete(powered);
}

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
    resize(1280, 800);

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_videoSink = new QVideoSink(this);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoSink(m_videoSink);
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
    connect(m_libraryPanel, &GaugeLibraryPanel::applyTemplateRequested, this, &MainWindow::applyTemplate);
    connect(m_libraryPanel, &GaugeLibraryPanel::libraryChanged, this, &MainWindow::rebuildOverlay);
    updateProperties();

    connect(m_videoSink, &QVideoSink::videoFrameChanged, m_canvas, &VideoCanvas::setFrame);
    connect(m_player, &QMediaPlayer::positionChanged, m_canvas, &VideoCanvas::setPosition);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::onPositionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::onDurationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MainWindow::onPlayerError);
    // a track just opened is put at its place once it can be
    connect(m_musicPlayer, &QMediaPlayer::mediaStatusChanged, this, &MainWindow::syncMusic);
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
    m_defaultLayout = saveState();
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

    // what the offset is based on can be a long sentence: it must not hold the window wide
    m_syncLabel = new ElidedLabel(central);

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
    // the route map is the Laps panel's: what is picked in the data is shown on it
    connect(m_dataPanel, &DataPanel::stretchPicked, this, [this](double start, double end)
    {
        m_lapsPanel->setHighlight(start, end);
    });
    connect(m_dataPanel, &DataPanel::editsChanged, this, &MainWindow::setDataEdits);
    // one smoothing for all charts
    connect(m_dataPanel, &DataPanel::smoothingChanged, m_timelinePanel, &TimelinePanel::setSmoothing);
    connect(m_dataPanel, &DataPanel::smoothingChanged, m_comparePanel, &LapComparePanel::setSmoothing);
    connect(m_timelinePanel, &TimelinePanel::smoothingChanged, m_dataPanel, &DataPanel::setSmoothing);
    connect(m_timelinePanel, &TimelinePanel::smoothingChanged, m_comparePanel, &LapComparePanel::setSmoothing);
    connect(m_comparePanel, &LapComparePanel::smoothingChanged, m_dataPanel, &DataPanel::setSmoothing);
    connect(m_comparePanel, &LapComparePanel::smoothingChanged, m_timelinePanel, &TimelinePanel::setSmoothing);
    m_musicDock = addPlaceholderDock("musicDock", tr("Music"), Qt::BottomDockWidgetArea);
    tabifyDockWidget(m_dataDock, m_musicDock);
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
        m_player->setPosition(qint64(std::llround(seconds * 1000.0)));
    });
    m_lapsPanel = new LapsPanel(m_lapsDock);
    m_lapsDock->setWidget(m_lapsPanel);
    m_lapsPanel->setUnits(m_units);
    connect(m_lapsPanel, &LapsPanel::lineRequested, this, &MainWindow::placeStartFinish);
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
    m_elevationCombo->addItem(tr("Recorded; the terrain's when there is none"), int(Terrain::WhenMissing));
    m_elevationCombo->addItem(tr("From the terrain model"), int(Terrain::Always));
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
    m_windCombo->addItem(tr("From weather history"), false);
    m_windCombo->addItem(tr("By hand:"), true);
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
    QHBoxLayout *windRow = new QHBoxLayout;
    windRow->addWidget(new QLabel(tr("Wind:"), sources));
    windRow->addWidget(m_windCombo, 1);
    windRow->addWidget(m_windSpeedSpin);
    windRow->addWidget(m_windFromSpin);
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
    fileMenu->addAction(tr("Save the &gauges as a template..."), this, &MainWindow::saveAsTemplate);
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
    viewMenu->addAction(m_lapsDock->toggleViewAction());
    viewMenu->addAction(m_musicDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Reset layout"), this, [this]()
    {
        restoreState(m_defaultLayout);
        // a dock closed before is brought back with the rest
        for (QDockWidget *dock : findChildren<QDockWidget *>())
            {
                dock->setFloating(false);
            }
        m_lapsDock->raise();
    });

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
    m_musicPanel->setCuts(m_project.cuts);
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
    // only the last load counts, also when the same file is loaded again before the first is done
    const quint64 generation = ++m_importGeneration;
    statusBar()->showMessage(tr("Loading %1...").arg(QFileInfo(path).fileName()));

    const QVector<DataEdit> edits = m_project.dataEdits;
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
    watcher->setFuture(QtConcurrent::run([path, edits, elevation, rider, surfaces, weather, wind, drivetrain]()
    {
        ImportResult result;
        result.raw = TelemetryImporter::loadRecorded(path, &result.error);
        result.session = workedOut(result.raw, edits, elevation, rider, surfaces, weather, wind, drivetrain, &result.waiting,
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
    m_undoStack->clear();
    setModified(false);
    m_session.reset();
    m_recordedSession.reset();
    m_rawSession.reset();
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
    const int pending = TileStore::instance()->pending();
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
    m_player->pause();
    Soundtrack soundtrack = m_project.soundtrack;
    soundtrack.tracks.erase(std::remove_if(soundtrack.tracks.begin(), soundtrack.tracks.end(), [](const MusicTrack &track)
    {
        return !QFileInfo::exists(track.path);
    }), soundtrack.tracks.end());
    ExportDialog dialog(m_project.videoPath, Overlay(m_project.overlay, m_session, m_units, m_project.theme),
                        m_project.sync(), m_project.cuts, soundtrack, m_player->position() / 1000.0, this);
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

void MainWindow::setGaugeCloseUp(int index, double meters)
{
    if (index < 0 || index >= m_project.overlay.size())
        {
            return;
        }
    OverlayState after{m_project.overlay, m_project.theme};
    after.items[index].closeUp = meters;
    changeOverlay(meters > 0.0 ? tr("Show the route close up") : tr("Show the whole route"), after);
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
    if (index < overlay.count() && overlay.hasRoute(index))
        {
            QAction *streets = menu.addAction(tr("Street map"));
            streets->setCheckable(true);
            streets->setChecked(overlay.hasStreets(index));
            connect(streets, &QAction::triggered, this, [this, index](bool shown)
            {
                setGaugeStreets(index, shown);
            });
            // the whole route, or this much of it around the rider
            QMenu *closeUp = menu.addMenu(tr("Close-up"));
            const double current = overlay.closeUp(index);
            const QVector<QPair<QString, double>> choices
            {
                {tr("Off: the whole route"), 0.0}, {tr("300 m"), 300.0}, {tr("600 m"), 600.0},
                {tr("1 km"), 1000.0}, {tr("2 km"), 2000.0}, {tr("5 km"), 5000.0},
            };
            for (const auto &choice : choices)
                {
                    QAction *action = closeUp->addAction(choice.first);
                    action->setCheckable(true);
                    action->setChecked(choice.second == current);
                    const double meters = choice.second;
                    connect(action, &QAction::triggered, this, [this, index, meters]()
                    {
                        setGaugeCloseUp(index, meters);
                    });
                }
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
    m_removeGaugeButton = new QPushButton(tr("Remove gauge"), panel);
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
    form->addRow(QString(), m_removeGaugeButton);
    QLabel *colorsTitle = new QLabel(tr("All gauges"), panel);
    colorsTitle->setToolTip(tr("Colors and font of every gauge on the video"));
    QFont bold = colorsTitle->font();
    bold.setBold(true);
    colorsTitle->setFont(bold);
    form->addRow(colorsTitle);
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
        OverlayState after{m_project.overlay, m_project.theme};
        after.theme.font = m_fontCombo->currentData().toString();
        after.theme.fontWeight = m_fontWeightCombo->currentData().toInt();
        after.theme.fontEverywhere = m_fontEverywhereCheck->isChecked();
        if (after.theme != m_project.theme)
            {
                changeOverlay(tr("Change gauge font"), after);
            }
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
    changeOverlay(tr("Change gauge colors"), after);
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
    for (int role = 0; role < m_themeButtons.size(); ++role)
        {
            const QColor color = m_project.theme.*(kThemeRoles[role].color);
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
        int row = m_fontCombo->findData(m_project.theme.font);
        if (row < 0)
            {
                // a font this computer does not have: still shown as the one chosen
                m_fontCombo->addItem(m_project.theme.font, m_project.theme.font);
                row = m_fontCombo->count() - 1;
            }
        m_fontCombo->setCurrentIndex(row);
        m_fontWeightCombo->setCurrentIndex(std::max(0, m_fontWeightCombo->findData(m_project.theme.fontWeight)));
        m_fontEverywhereCheck->setChecked(m_project.theme.fontEverywhere);
        m_fontEverywhereCheck->setEnabled(!m_project.theme.font.isEmpty() || m_project.theme.fontWeight > 0);
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
    m_heldTime = -1.0;
    m_rawSession = raw;
    m_recordedSession = session;
    m_dataPanel->setSessions(raw, session, m_units);
    m_session = LapEngine::apply(session, m_project.startFinish, m_project.sectors);
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
    static_cast<ElidedLabel *>(m_syncLabel)->setFullText(tr("Set by hand."));
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
    static_cast<ElidedLabel *>(m_syncLabel)->setFullText(explanation);
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
                    item->setText(MinColumn, QLocale().toString(display.convert(channel->minValue()), 'f', display.decimals));
                    item->setText(MaxColumn, QLocale().toString(display.convert(channel->maxValue()), 'f', display.decimals));
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
    m_session = LapEngine::apply(m_recordedSession, m_project.startFinish, m_project.sectors);
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

void MainWindow::syncMusic()
{
    const Soundtrack &soundtrack = m_heardSoundtrack;
    m_audioOutput->setVolume(float(soundtrack.videoGain()));
    const double videoTime = m_player->position() / 1000.0;
    const bool video = !m_player->source().isEmpty();
    const Soundtrack::Moment moment = video && soundtrack.hasMusic()
                                      ? soundtrack.at(soundtrack.musicTime(videoTime, m_project.cuts)) : Soundtrack::Moment();
    if (moment.track < 0)
        {
            if (m_musicPlayer->playbackState() != QMediaPlayer::StoppedState)
                {
                    m_musicPlayer->stop();
                }
            m_musicTrack = -1;
            return;
        }
    const QString path = soundtrack.tracks.at(moment.track).path;
    if (path != m_musicSource)
        {
            m_musicSource = path;
            m_musicPlayer->setSource(QUrl::fromLocalFile(path));
        }
    m_musicOutput->setVolume(float(moment.gain * soundtrack.musicGain()));
    if (m_player->playbackState() != QMediaPlayer::PlayingState)
        {
            if (m_musicPlayer->playbackState() == QMediaPlayer::PlayingState)
                {
                    m_musicPlayer->pause();
                }
            return;
        }
    const QMediaPlayer::MediaStatus status = m_musicPlayer->mediaStatus();
    if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia || status == QMediaPlayer::LoadingMedia)
        {
            // a file shorter than it said, or one that cannot be played: silence, not again and again
            return;
        }
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
            m_musicPlayer->setPosition(wanted);
            m_musicPlayer->play();
        }
    else if (jumped || std::abs(m_musicPlayer->position() - wanted) > 1000)
        {
            // Put in step after a jump, or when the two have drifted clearly apart. Not for less:
            // both players tell where they are only roughly, and each seek is heard.
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

void MainWindow::recomputeData()
{
    if (!m_rawSession)
        {
            return;
        }
    m_sinceRecompute.start();
    m_recordedSession = workedOut(m_rawSession, m_project.dataEdits, m_project.elevation, m_rider, m_project.surfaces, m_weather,
                                  m_project.wind, m_drivetrain,
                                  &m_terrainWaiting, &m_weatherWaiting);
    m_dataPanel->setSessions(m_rawSession, m_recordedSession, m_units);
    updateLaps();
}

void MainWindow::seekTelemetryTime(double time)
{
    // telemetry time = video time + offset
    const double videoTime = m_project.sync().videoTime(time);
    const double videoEnd = m_player->duration() / 1000.0;
    const qint64 position = std::clamp<qint64>(qint64(std::llround(videoTime * 1000.0)), 0, std::max<qint64>(0, m_player->duration()));
    if (!m_project.videoPath.isEmpty())
        {
            m_player->setPosition(position);
        }
    // a moment the video has no picture for is still shown in the data's own panels
    const bool outside = m_project.videoPath.isEmpty() || videoTime < 0.0 || (videoEnd > 0.0 && videoTime > videoEnd);
    m_heldTime = outside ? time : -1.0;
    m_heldVideoPosition = outside ? (m_project.videoPath.isEmpty() ? -1 : position) : -1;
    m_dataPanel->setTime(shownTelemetryTime());
    m_lapsPanel->setTime(shownTelemetryTime());
}

double MainWindow::shownTelemetryTime() const
{
    if (m_heldTime >= 0.0)
        {
            // held until the video is played or moved
            if (m_project.videoPath.isEmpty() || std::llabs(m_player->position() - m_heldVideoPosition) < 50)
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
    QString address = AppSettings::mapTileUrl();
    QString credit = AppSettings::mapAttribution();
    int maxZoom = 19;
    if (AppSettings::mapProvider() != QLatin1String(AppSettings::OwnMapServer))
        {
            const TileStore::Provider provider = TileStore::provider(AppSettings::mapProvider());
            address = provider.urlTemplate;
            address.replace("{apikey}", AppSettings::mapApiKey());
            credit = provider.attribution;
            maxZoom = provider.maxZoom;
        }
    maps->setSource(address, AppSettings::mapCreditShown() ? credit : QString(), maxZoom);
    maps->setDownloads(AppSettings::mapDownloads());
    TileStore::terrain()->setDownloads(AppSettings::mapDownloads());
    // asked for only when the Weather page of the settings says so
    WeatherStore::instance()->setDownloads(true);
    m_lapsPanel->refreshMapChoice();
    if (maps->urlTemplate() + maps->attribution() != before)
        {
            rebuildOverlay();
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
