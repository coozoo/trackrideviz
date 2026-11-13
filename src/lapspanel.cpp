#include "lapspanel.h"

#include "telemetry/channelids.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
const double kPi = 3.14159265358979323846;

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

const QColor kSectorColour(40, 120, 220);
// the fastest of a sector, as timing screens show it
const QColor kBestSectorColour(150, 60, 200);
}

// The route from above, north up, fitted into the widget.
class TrackView : public QWidget
{
public:
    explicit TrackView(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(160, 140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    std::function<void(double, double)> clicked;

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors)
    {
        m_session = session;
        m_line = line;
        m_sectors = sectors;
        m_points.clear();
        const Channel *lat = session ? session->channel(ChannelId::PositionLat) : nullptr;
        const Channel *lon = session ? session->channel(ChannelId::PositionLon) : nullptr;
        if (lat && lon && lat->count() >= 2)
            {
                // no more points than pixels need
                const int step = std::max(1, lat->count() / 3000);
                for (int i = 0; i < lat->count(); i += step)
                    {
                        m_points.append(QPointF(lon->valueAt(lat->timeAt(i)), lat->valueAtIndex(i)));
                    }
                double middle = 0.0;
                for (const QPointF &p : m_points)
                    {
                        middle += p.y();
                    }
                m_lonScale = std::cos(middle / m_points.size() * kPi / 180.0);
            }
        update();
    }

    void setTime(double time)
    {
        m_time = time;
        update();
    }

    // what a click places, with a hint on the map; empty: nothing
    void setPlacing(const QString &hint)
    {
        m_placing = hint;
        setCursor(hint.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        if (m_points.size() < 2)
            {
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, LapsPanel::tr("No positions in the data"));
                return;
            }
        const QTransform map = toWidget();
        QPainterPath route;
        route.moveTo(map.map(projected(m_points.first())));
        for (int i = 1; i < m_points.size(); ++i)
            {
                route.lineTo(map.map(projected(m_points.at(i))));
            }
        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.drawPath(route);

        for (const StartFinishLine &sector : m_sectors)
            {
                drawLine(painter, map, sector, kSectorColour);
            }
        if (m_line.isValid())
            {
                drawLine(painter, map, m_line, QColor(220, 40, 40));
            }
        const Channel *lat = m_session->channel(ChannelId::PositionLat);
        const Channel *lon = m_session->channel(ChannelId::PositionLon);
        bool ok = false;
        const double la = lat->valueAt(m_time, &ok);
        if (ok)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette().color(QPalette::Highlight));
                painter.drawEllipse(map.map(projected(QPointF(lon->valueAt(m_time), la))), 5, 5);
            }
        if (!m_placing.isEmpty())
            {
                painter.setPen(palette().color(QPalette::Highlight));
                painter.drawText(rect().adjusted(6, 6, -6, -6), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                                 m_placing);
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_placing.isEmpty() || m_points.size() < 2 || event->button() != Qt::LeftButton || !clicked)
            {
                return;
            }
        const QPointF p = toWidget().inverted().map(event->position());
        clicked(-p.y(), p.x() / m_lonScale);
    }

private:
    // drawn long enough to be seen on a long route
    void drawLine(QPainter &painter, const QTransform &map, const StartFinishLine &line, const QColor &colour) const
    {
        const QPointF a = map.map(projected(QPointF(line.lon1, line.lat1)));
        const QPointF b = map.map(projected(QPointF(line.lon2, line.lat2)));
        const QPointF middle = (a + b) / 2.0;
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const QPointF half = length > 0.0 ? (b - a) / length * std::max(length, 16.0) / 2.0 : QPointF();
        painter.setPen(QPen(colour, 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(middle - half, middle + half);
    }

    // degrees -> a plane with north up: x = lon * cos(lat), y = -lat
    QPointF projected(const QPointF &lonLat) const
    {
        return QPointF(lonLat.x() * m_lonScale, -lonLat.y());
    }

    QTransform toWidget() const
    {
        QRectF bounds;
        for (const QPointF &p : m_points)
            {
                const QPointF q = projected(p);
                bounds = bounds.isNull() ? QRectF(q, QSizeF(1e-9, 1e-9)) : bounds.united(QRectF(q, QSizeF(1e-9, 1e-9)));
            }
        const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -12);
        const double scale = std::min(area.width() / std::max(bounds.width(), 1e-9),
                                      area.height() / std::max(bounds.height(), 1e-9));
        QTransform t;
        t.translate(area.center().x(), area.center().y());
        t.scale(scale, scale);
        t.translate(-bounds.center().x(), -bounds.center().y());
        return t;
    }

    TelemetrySessionPtr m_session;
    StartFinishLine m_line;
    QVector<StartFinishLine> m_sectors;
    QVector<QPointF> m_points;   // lon, lat
    double m_lonScale{1.0};
    double m_time{0.0};
    QString m_placing;
};

LapsPanel::LapsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_track = new TrackView(this);
    m_track->clicked = [this](double lat, double lon)
    {
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

    m_info = new QLabel(this);
    m_info->setWordWrap(true);

    m_lapTable = new QTreeWidget(this);
    m_lapTable->setRootIsDecorated(false);
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
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_track, 2);
    layout->addLayout(buttons);
    layout->addLayout(sectorButtons);
    layout->addWidget(m_info);
    layout->addWidget(m_lapTable, 1);
    setSession(nullptr, StartFinishLine(), {});
}

void LapsPanel::setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                           const QVector<StartFinishLine> &sectors)
{
    m_session = session;
    m_line = line;
    m_sectors = sectors;
    m_track->setSession(session, line, sectors);
    const bool positions = session && session->channel(ChannelId::PositionLat);
    for (QPushButton *button : {m_placeButton, m_sectorButton})
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
                                    item->setForeground(FirstSectorColumn + k, kBestSectorColour);
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
            QPushButton *other = button == m_placeButton ? m_sectorButton : m_placeButton;
            const QSignalBlocker blocker(other);
            other->setChecked(false);
        }
    m_track->setPlacing(!checked ? QString()
                        : button == m_placeButton ? tr("Click on the route where laps begin")
                        : tr("Click on the route where a sector ends"));
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
                            : tr("The route does not cross the start/finish line twice: no full lap."));
        }
    else
        {
            m_info->setText(laps > 0 ? tr("%n lap(s) as the device recorded them. Place a start/finish line to time laps "
                                          "on a track.", nullptr, laps)
                            : tr("No laps. Place a start/finish line to time laps on a track."));
        }
}
