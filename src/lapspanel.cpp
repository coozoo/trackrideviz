#include "lapspanel.h"

#include "telemetry/channelids.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

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

enum LapColumn { LapNumber, LapTimeColumn, GapColumn };
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

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line)
    {
        m_session = session;
        m_line = line;
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

    void setPlacing(bool placing)
    {
        m_placing = placing;
        setCursor(placing ? Qt::CrossCursor : Qt::ArrowCursor);
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

        if (m_line.isValid())
            {
                // drawn long enough to be seen on a long route
                const QPointF a = map.map(projected(QPointF(m_line.lon1, m_line.lat1)));
                const QPointF b = map.map(projected(QPointF(m_line.lon2, m_line.lat2)));
                const QPointF middle = (a + b) / 2.0;
                const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
                const QPointF half = length > 0.0 ? (b - a) / length * std::max(length, 16.0) / 2.0 : QPointF();
                painter.setPen(QPen(QColor(220, 40, 40), 3, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(middle - half, middle + half);
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
        if (m_placing)
            {
                painter.setPen(palette().color(QPalette::Highlight));
                painter.drawText(rect().adjusted(6, 6, -6, -6), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                                 LapsPanel::tr("Click on the route where laps begin"));
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_placing || m_points.size() < 2 || event->button() != Qt::LeftButton || !clicked)
            {
                return;
            }
        const QPointF p = toWidget().inverted().map(event->position());
        clicked(-p.y(), p.x() / m_lonScale);
    }

private:
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
    QVector<QPointF> m_points;   // lon, lat
    double m_lonScale{1.0};
    double m_time{0.0};
    bool m_placing{false};
};

LapsPanel::LapsPanel(QWidget *parent)
    : QWidget(parent)
{
    m_track = new TrackView(this);
    m_track->clicked = [this](double lat, double lon)
    {
        m_placeButton->setChecked(false);
        emit lineRequested(lat, lon);
    };

    m_placeButton = new QPushButton(tr("Place start/finish"), this);
    m_placeButton->setCheckable(true);
    m_placeButton->setToolTip(tr("Then click on the map where laps begin: the line is put across the route there"));
    connect(m_placeButton, &QPushButton::toggled, this, &LapsPanel::placeToggled);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_removeButton->setToolTip(tr("Use the laps the device recorded instead"));
    connect(m_removeButton, &QPushButton::clicked, this, &LapsPanel::removeLineRequested);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);

    m_lapTable = new QTreeWidget(this);
    m_lapTable->setHeaderLabels({tr("Lap"), tr("Time"), tr("To best")});
    m_lapTable->setRootIsDecorated(false);
    m_lapTable->setUniformRowHeights(true);
    m_lapTable->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_lapTable->setToolTip(tr("Double-click a lap to go to its start"));
    connect(m_lapTable, &QTreeWidget::itemActivated, this, &LapsPanel::lapActivated);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_placeButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(m_track, 2);
    layout->addLayout(buttons);
    layout->addWidget(m_info);
    layout->addWidget(m_lapTable, 1);
    setSession(nullptr, StartFinishLine());
}

void LapsPanel::setSession(const TelemetrySessionPtr &session, const StartFinishLine &line)
{
    m_session = session;
    m_line = line;
    m_track->setSession(session, line);
    const bool positions = session && session->channel(ChannelId::PositionLat);
    m_placeButton->setEnabled(positions);
    if (!positions)
        {
            m_placeButton->setChecked(false);
        }
    m_removeButton->setEnabled(line.isValid());

    m_lapTable->clear();
    const QVector<TelemetrySession::Lap> laps = session ? session->laps() : QVector<TelemetrySession::Lap>();
    double best = 0.0;
    int bestRow = -1;
    for (int i = 0; i < laps.size(); ++i)
        {
            const double duration = laps.at(i).endTime - laps.at(i).startTime;
            if (bestRow < 0 || duration < best)
                {
                    best = duration;
                    bestRow = i;
                }
        }
    for (int i = 0; i < laps.size(); ++i)
        {
            const double duration = laps.at(i).endTime - laps.at(i).startTime;
            QTreeWidgetItem *item = new QTreeWidgetItem(m_lapTable);
            item->setText(LapNumber, QString::number(i + 1));
            item->setText(LapTimeColumn, lapTime(duration));
            item->setText(GapColumn, i == bestRow ? tr("best") : QString("+%1").arg(duration - best, 0, 'f', 3));
            item->setData(LapNumber, Qt::UserRole, laps.at(i).startTime);
            item->setTextAlignment(LapTimeColumn, Qt::AlignRight | Qt::AlignVCenter);
            item->setTextAlignment(GapColumn, Qt::AlignRight | Qt::AlignVCenter);
            if (i == bestRow)
                {
                    QFont font = item->font(LapNumber);
                    font.setBold(true);
                    for (int column : {LapNumber, LapTimeColumn, GapColumn})
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

void LapsPanel::placeToggled(bool checked)
{
    m_track->setPlacing(checked);
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
