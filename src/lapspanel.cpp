#include "lapspanel.h"

#include "maps/tilestore.h"
#include "settings/appsettings.h"
#include "trackview.h"
#include "telemetry/channelids.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{

QString lapTime(double seconds)
{
    const int minutes = int(seconds / 60.0);
    return QString("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, 6, 'f', 3, QLatin1Char('0'));
}

// short sectors as seconds, long ones like a lap
QString sectorTimeText(double seconds)
{
    return seconds < 60.0 ? QString::number(seconds, 'f', 3) : lapTime(seconds);
}

enum LapColumn { LapNumber, LapTimeColumn, FirstSectorColumn };

const QColor kSectorColor(40, 120, 220);
// the fastest of a sector, as timing screens show it
const QColor kBestSectorColor(150, 60, 200);
}

LapsPanel::LapsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_track = new TrackView(this);
    m_track->clicked = [this](double lat, double lon)
    {
        if (m_surfaceButton->isChecked())
            {
                surfaceClicked(lat, lon);
                return;
            }
        const bool sector = m_sectorButton->isChecked();
        m_placeButton->setChecked(false);
        m_sectorButton->setChecked(false);
        if (sector)
            {
                emit sectorRequested(lat, lon);
            }
        else
            {
                emit lineRequested(lat, lon);
            }
    };

    m_placeButton = new QPushButton(tr("Place start/finish"), this);
    m_placeButton->setCheckable(true);
    m_placeButton->setToolTip(tr("Then click on the map where laps begin: the line is put across the route there"));
    connect(m_placeButton, &QPushButton::toggled, this, [this](bool checked) { placeToggled(m_placeButton, checked); });
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setToolTip(tr("Use the laps the device recorded instead"));
    connect(m_removeButton, &QPushButton::clicked, this, &LapsPanel::removeLineRequested);
    m_sectorButton = new QPushButton(tr("Add sector"), this);
    m_sectorButton->setCheckable(true);
    m_sectorButton->setToolTip(tr("Then click on the map where a sector ends: each line splits the lap once more"));
    connect(m_sectorButton, &QPushButton::toggled, this, [this](bool checked) { placeToggled(m_sectorButton, checked); });
    m_clearSectorsButton = new QPushButton(tr("Clear sectors"), this);
    connect(m_clearSectorsButton, &QPushButton::clicked, this, &LapsPanel::removeSectorsRequested);

    m_surfaceButton = new QPushButton(tr("Mark surface"), this);
    m_surfaceButton->setCheckable(true);
    m_surfaceButton->setToolTip(tr("Then click on the map where a stretch begins and where it ends, and choose what it is "
                                   "ridden on: the power estimate counts it in"));
    connect(m_surfaceButton, &QPushButton::toggled, this, [this](bool checked) { placeToggled(m_surfaceButton, checked); });
    m_clearSurfacesButton = new QPushButton(tr("Clear all"), this);
    connect(m_clearSurfacesButton, &QPushButton::clicked, this, &LapsPanel::removeSurfacesRequested);
    m_removeSurfaceButton = new QPushButton(tr("Remove"), this);
    m_removeSurfaceButton->setToolTip(tr("Takes the surface picked in the table off the route"));
    connect(m_removeSurfaceButton, &QPushButton::clicked, this, [this]()
    {
        const int row = m_surfaceTable->currentRow();
        if (row >= 0 && row < m_surfaces.size())
            {
                QVector<SurfaceStretch> surfaces = m_surfaces;
                surfaces.removeAt(row);
                emit surfacesEdited(surfaces);
            }
    });
    m_surfaceTable = new QTableWidget(0, 4, this);
    m_surfaceTable->setHorizontalHeaderLabels({tr("Surface"), tr("From"), tr("To"), tr("Length")});
    m_surfaceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_surfaceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_surfaceTable->verticalHeader()->hide();
    m_surfaceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_surfaceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_surfaceTable->setMinimumHeight(60);
    m_surfaceTable->setToolTip(tr("What each stretch is ridden on and where it begins and ends along the ride; "
                                  "where two overlap, the lower one in the list counts"));
    connect(m_surfaceTable, &QTableWidget::currentCellChanged, this, [this](int row)
    {
        m_track->setPickedSurface(row);
        m_removeSurfaceButton->setEnabled(row >= 0);
    });

    m_streetsCheck = new QCheckBox(tr("Street map"), this);
    m_streetsCheck->setChecked(AppSettings::lapsMapShown());
    m_mapCombo = new QComboBox(this);
    m_mapCombo->setToolTip(tr("The map here and under the gauges that show one"));
    for (const TileStore::Provider &provider : TileStore::providers())
        {
            m_mapCombo->addItem(provider.name, provider.id);
        }
    m_mapCombo->addItem(tr("Own server"), QString(AppSettings::OwnMapServer));
    connect(m_streetsCheck, &QCheckBox::toggled, this, [this](bool shown)
    {
        AppSettings::setLapsMapShown(shown);
        refreshMapChoice();
        // the Data viewer's map follows
        emit mapChoiceChanged();
    });
    connect(m_mapCombo, &QComboBox::activated, this, [this]()
    {
        AppSettings::setMapProvider(m_mapCombo->currentData().toString());
        emit mapChoiceChanged();
    });
    // tiles come one by one
    connect(TileStore::instance(), &TileStore::arrived, m_track, qOverload<>(&QWidget::update));
    refreshMapChoice();

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    // a long line wraps rather than making the dock taller than it needs
    m_info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_lapTable = new QTreeWidget(this);
    m_lapTable->setRootIsDecorated(false);
    m_lapTable->setMinimumHeight(60);
    m_lapTable->setUniformRowHeights(true);
    m_lapTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_lapTable->setToolTip(tr("Double-click a lap to go to its start; select several to compare them"));
    connect(m_lapTable, &QTreeWidget::itemActivated, this, &LapsPanel::lapActivated);
    m_lapTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_lapTable, &QTreeWidget::itemSelectionChanged, this, [this]()
    {
        QVector<int> laps;
        for (const QTreeWidgetItem *item : m_lapTable->selectedItems())
            {
                laps.append(m_lapTable->indexOfTopLevelItem(const_cast<QTreeWidgetItem *>(item)));
            }
        std::sort(laps.begin(), laps.end());
        emit lapsSelected(laps);
    });

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_placeButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    QHBoxLayout *sectorButtons = new QHBoxLayout;
    sectorButtons->addWidget(m_sectorButton);
    sectorButtons->addWidget(m_clearSectorsButton);
    sectorButtons->addStretch(1);
    QHBoxLayout *surfaceButtons = new QHBoxLayout;
    surfaceButtons->addWidget(m_surfaceButton);
    surfaceButtons->addWidget(m_clearSurfacesButton);
    surfaceButtons->addStretch(1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    QHBoxLayout *mapChoice = new QHBoxLayout;
    mapChoice->addWidget(m_streetsCheck);
    mapChoice->addWidget(m_mapCombo, 1);
    layout->addWidget(m_track, 2);
    layout->addLayout(mapChoice);
    surfaceButtons->insertWidget(1, m_removeSurfaceButton);
    QWidget *lapsTab = new QWidget(this);
    QVBoxLayout *lapsLayout = new QVBoxLayout(lapsTab);
    lapsLayout->setContentsMargins(0, 6, 0, 0);
    lapsLayout->addLayout(buttons);
    lapsLayout->addLayout(sectorButtons);
    lapsLayout->addWidget(m_info);
    lapsLayout->addWidget(m_lapTable, 1);
    QWidget *surfacesTab = new QWidget(this);
    QVBoxLayout *surfacesLayout = new QVBoxLayout(surfacesTab);
    surfacesLayout->setContentsMargins(0, 6, 0, 0);
    surfacesLayout->addLayout(surfaceButtons);
    surfacesLayout->addWidget(m_surfaceTable, 1);
    QTabWidget *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(lapsTab, tr("Laps"));
    tabs->addTab(surfacesTab, tr("Surfaces"));
    // what is being placed belongs to its tab
    connect(tabs, &QTabWidget::currentChanged, this, [this]()
    {
        for (QPushButton *button : {m_placeButton, m_sectorButton, m_surfaceButton})
            {
                button->setChecked(false);
            }
    });
    layout->addWidget(tabs, 1);
    setSession(nullptr, StartFinishLine(), {});
    setSurfaces({});
}

void LapsPanel::refreshMapChoice()
{
    const int index = m_mapCombo->findData(AppSettings::mapProvider());
    m_mapCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_mapCombo->setEnabled(m_streetsCheck->isChecked());
    m_track->setStreets(m_streetsCheck->isChecked(), AppSettings::mapCredit());
}

void LapsPanel::setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                           const QVector<StartFinishLine> &sectors)
{
    m_session = session;
    m_line = line;
    m_sectors = sectors;
    m_track->setSession(session, line, sectors);
    const bool positions = session && session->channel(ChannelId::PositionLat);
    for (QPushButton *button : {m_placeButton, m_sectorButton, m_surfaceButton})
        {
            button->setEnabled(positions);
            if (!positions)
                {
                    button->setChecked(false);
                }
        }
    m_removeButton->setEnabled(line.isValid());
    m_clearSectorsButton->setEnabled(!sectors.isEmpty());

    const QVector<TelemetrySession::Lap> laps = session ? session->laps() : QVector<TelemetrySession::Lap>();
    // sector columns only when some lap has its sector times
    int sectorCount = 0;
    for (const TelemetrySession::Lap &lap : laps)
        {
            if (!lap.splits.isEmpty())
                {
                    sectorCount = int(lap.splits.size()) + 1;
                }
        }
    QStringList headers{tr("Lap"), tr("Time")};
    for (int k = 0; k < sectorCount; ++k)
        {
            headers << tr("S%1").arg(k + 1);
        }
    headers << tr("To best");
    const int gapColumn = int(headers.size()) - 1;
    m_lapTable->clear();
    m_lapTable->setColumnCount(int(headers.size()));
    m_lapTable->setHeaderLabels(headers);

    auto sectorTime = [&](const TelemetrySession::Lap &lap, int k)
    {
        const double from = k == 0 ? lap.startTime : lap.splits.at(k - 1);
        const double to = k + 1 == sectorCount ? lap.endTime : lap.splits.at(k);
        return to - from;
    };
    double best = 0.0;
    int bestRow = -1;
    QVector<int> bestSector(sectorCount, -1);
    int lapsWithSectors = 0;
    for (int i = 0; i < laps.size(); ++i)
        {
            const double duration = laps.at(i).endTime - laps.at(i).startTime;
            if (bestRow < 0 || duration < best)
                {
                    best = duration;
                    bestRow = i;
                }
            if (laps.at(i).splits.size() + 1 == sectorCount)
                {
                    ++lapsWithSectors;
                    for (int k = 0; k < sectorCount; ++k)
                        {
                            if (bestSector.at(k) < 0 || sectorTime(laps.at(i), k) < sectorTime(laps.at(bestSector.at(k)), k))
                                {
                                    bestSector[k] = i;
                                }
                        }
                }
        }
    for (int i = 0; i < laps.size(); ++i)
        {
            const TelemetrySession::Lap &lap = laps.at(i);
            const double duration = lap.endTime - lap.startTime;
            QTreeWidgetItem *item = new QTreeWidgetItem(m_lapTable);
            item->setText(LapNumber, QString::number(i + 1));
            item->setText(LapTimeColumn, lapTime(duration));
            item->setText(gapColumn, i == bestRow ? tr("best") : QString("+%1").arg(duration - best, 0, 'f', 3));
            item->setData(LapNumber, Qt::UserRole, lap.startTime);
            for (int column = LapTimeColumn; column <= gapColumn; ++column)
                {
                    item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
                }
            if (lap.splits.size() + 1 == sectorCount)
                {
                    for (int k = 0; k < sectorCount; ++k)
                        {
                            item->setText(FirstSectorColumn + k, sectorTimeText(sectorTime(lap, k)));
                            // nothing is fastest with nothing to compare
                            if (bestSector.at(k) == i && lapsWithSectors > 1)
                                {
                                    item->setForeground(FirstSectorColumn + k, kBestSectorColor);
                                    item->setToolTip(FirstSectorColumn + k, tr("The fastest of this sector"));
                                }
                        }
                }
            if (i == bestRow)
                {
                    QFont font = item->font(LapNumber);
                    font.setBold(true);
                    for (int column = 0; column <= gapColumn; ++column)
                        {
                            item->setFont(column, font);
                        }
                }
        }
    updateInfo();
}

void LapsPanel::setTime(double time)
{
    m_track->setTime(time);
}

void LapsPanel::placeToggled(QPushButton *button, bool checked)
{
    if (checked)
        {
            // one thing at a time
            for (QPushButton *other : {m_placeButton, m_sectorButton, m_surfaceButton})
                {
                    if (other != button)
                        {
                            const QSignalBlocker blocker(other);
                            other->setChecked(false);
                        }
                }
        }
    m_surfaceBegun = false;
    m_track->setMark(false);
    m_track->setPlacing(!checked ? QString()
                        : button == m_placeButton ? tr("Click on the route where laps begin")
                        : button == m_sectorButton ? tr("Click on the route where a sector ends")
                        : tr("Click on the route where the surface begins"));
}

void LapsPanel::surfaceClicked(double lat, double lon)
{
    if (!m_surfaceBegun)
        {
            m_surfaceBegun = true;
            m_surfaceLat = lat;
            m_surfaceLon = lon;
            m_track->setMark(true, lat, lon);
            m_track->setPlacing(tr("Click on the route where this surface ends"));
            return;
        }
    QMenu menu(this);
    for (const SurfaceType &type : AppSettings::surfaceTypes())
        {
            QAction *action = menu.addAction(tr("%1 (×%2)").arg(type.name).arg(type.factor, 0, 'g', 3));
            action->setData(QVariantList{type.name, type.factor});
        }
    const QAction *chosen = menu.exec(QCursor::pos());
    const double fromLat = m_surfaceLat;
    const double fromLon = m_surfaceLon;
    m_surfaceButton->setChecked(false);
    if (chosen)
        {
            const QVariantList type = chosen->data().toList();
            emit surfaceRequested(fromLat, fromLon, lat, lon, type.at(0).toString(), type.at(1).toDouble());
        }
}

void LapsPanel::setSurfaces(const QVector<SurfaceStretch> &surfaces)
{
    m_surfaces = surfaces;
    m_track->setSurfaces(surfaces);
    m_clearSurfacesButton->setEnabled(!surfaces.isEmpty());

    const int picked = m_surfaceTable->currentRow();
    const QSignalBlocker blocker(m_surfaceTable);
    m_surfaceTable->setRowCount(0);
    const Channel *distance = m_session ? m_session->channel(ChannelId::Distance) : nullptr;
    const double whole = distance && !distance->isEmpty() ? distance->maxValue() / 1000.0 : 100000.0;
    const QVector<SurfaceType> types = AppSettings::surfaceTypes();
    for (int row = 0; row < surfaces.size(); ++row)
        {
            const SurfaceStretch stretch = surfaces.at(row);
            m_surfaceTable->insertRow(row);
            QComboBox *kind = new QComboBox(m_surfaceTable);
            // a kind the settings no longer have stays what it was
            if (!types.contains(SurfaceType{stretch.surface, stretch.factor}))
                {
                    kind->addItem(tr("%1 (×%2)").arg(stretch.surface).arg(stretch.factor, 0, 'g', 3),
                                  QVariantList{stretch.surface, stretch.factor});
                }
            for (const SurfaceType &type : types)
                {
                    kind->addItem(tr("%1 (×%2)").arg(type.name).arg(type.factor, 0, 'g', 3), QVariantList{type.name, type.factor});
                    if (type.name == stretch.surface && type.factor == stretch.factor)
                        {
                            kind->setCurrentIndex(kind->count() - 1);
                        }
                }
            connect(kind, &QComboBox::activated, this, [this, row, kind]()
            {
                const QVariantList type = kind->currentData().toList();
                QVector<SurfaceStretch> edited = m_surfaces;
                edited[row].surface = type.at(0).toString();
                edited[row].factor = type.at(1).toDouble();
                // once this box is done with its click: the table is filled anew, and the box goes
                QTimer::singleShot(0, this, [this, edited]() { emit surfacesEdited(edited); });
            });
            m_surfaceTable->setCellWidget(row, 0, kind);
            const auto place = [&](int column, double meters)
            {
                QDoubleSpinBox *box = new QDoubleSpinBox(m_surfaceTable);
                box->setRange(0.0, whole);
                box->setDecimals(2);
                box->setSingleStep(0.1);
                box->setSuffix(tr(" km"));
                box->setValue(meters / 1000.0);
                connect(box, &QDoubleSpinBox::editingFinished, this, [this, row, column, box]()
                {
                    QVector<SurfaceStretch> edited = m_surfaces;
                    (column == 1 ? edited[row].from : edited[row].to) = box->value() * 1000.0;
                    if (edited.at(row) == m_surfaces.at(row))
                        {
                            return;
                        }
                    // an end before its beginning: as it was
                    const bool sound = edited.at(row).to > edited.at(row).from;
                    QTimer::singleShot(0, this, [this, edited, sound]()
                    {
                        sound ? emit surfacesEdited(edited) : setSurfaces(m_surfaces);
                    });
                });
                m_surfaceTable->setCellWidget(row, column, box);
            };
            place(1, stretch.from);
            place(2, stretch.to);
            QTableWidgetItem *length = new QTableWidgetItem(tr("%1 km").arg((stretch.to - stretch.from) / 1000.0, 0, 'f', 2));
            length->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_surfaceTable->setItem(row, 3, length);
        }
    const int row = std::min(picked, int(surfaces.size()) - 1);
    m_surfaceTable->setCurrentCell(row, 0);
    m_track->setPickedSurface(row);
    m_removeSurfaceButton->setEnabled(row >= 0);
}

void LapsPanel::lapActivated(QTreeWidgetItem *item)
{
    emit seekRequested(item->data(LapNumber, Qt::UserRole).toDouble());
}

void LapsPanel::updateInfo()
{
    if (!m_session)
        {
            m_info->setText(tr("Open data to see its laps."));
            return;
        }
    const int laps = int(m_session->laps().size());
    if (m_line.isValid())
        {
            m_info->setText(laps > 0 ? tr("%n lap(s) between crossings of the start/finish line.", nullptr, laps)
                            : tr("The route crosses the start/finish line only once. For a ride from one place to "
                                 "another add a sector line where the stretch ends: the last one is the finish."));
        }
    else
        {
            m_info->setText(laps > 0 ? tr("%n lap(s) as the device recorded them. Place a start/finish line to time laps "
                                          "on a track.", nullptr, laps)
                            : tr("No laps. Place a start/finish line to time laps on a track."));
        }
}
