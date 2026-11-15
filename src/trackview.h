#pragma once

#include "laps/lapengine.h"
#include "maps/tilestore.h"
#include "telemetry/channelids.h"
#include "telemetry/surfaces.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

// The route from above, north up, fitted into the widget. It can be looked at
// closer: the wheel or + and - zoom in and out, a drag moves the view (never
// so far that the route is out of it), and the button between them fits the
// whole route again.
class TrackView : public QWidget
{
public:
    explicit TrackView(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(120, 80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        const auto button = [this](const QString &text, const QString &tip, const std::function<void()> &action)
        {
            QToolButton *b = new QToolButton(this);
            b->setText(text);
            b->setToolTip(tip);
            b->setFixedSize(26, 26);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, action);
            return b;
        };
        m_zoomIn = button(QStringLiteral("+"), QCoreApplication::translate("TrackView", "Closer (also the wheel)"),
                          [this]() { zoomAround(QRectF(rect()).center(), 1.5); });
        m_fit = button(QStringLiteral("⤢"), QCoreApplication::translate("TrackView", "The whole route"), [this]()
        {
            m_zoom = 1.0;
            m_pan = QPointF();
            update();
        });
        m_zoomOut = button(QStringLiteral("−"), QCoreApplication::translate("TrackView", "Farther"),
                           [this]() { zoomAround(QRectF(rect()).center(), 1.0 / 1.5); });
    }

    // A click on the map while something is being placed (setPlacing): latitude and longitude.
    std::function<void(double, double)> clicked;

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors)
    {
        m_session = session;
        m_line = line;
        m_sectors = sectors;
        // another ride is seen whole; the same one worked out again stays as it was looked at
        const QPointF firstBefore = m_points.isEmpty() ? QPointF() : m_points.first();
        m_points.clear();
        m_times.clear();
        m_distances.clear();
        const Channel *distance = session ? session->channel(ChannelId::Distance) : nullptr;
        const Channel *lat = session ? session->channel(ChannelId::PositionLat) : nullptr;
        const Channel *lon = session ? session->channel(ChannelId::PositionLon) : nullptr;
        if (lat && lon && lat->count() >= 2)
            {
                // no more points than pixels need
                const int step = std::max(1, lat->count() / 3000);
                for (int i = 0; i < lat->count(); i += step)
                    {
                        m_points.append(QPointF(lon->valueAt(lat->timeAt(i)), lat->valueAtIndex(i)));
                        m_times.append(lat->timeAt(i));
                        m_distances.append(distance && !distance->isEmpty() ? distance->valueAt(lat->timeAt(i)) : 0.0);
                    }
            }
        if (m_points.isEmpty() || m_points.first() != firstBefore)
            {
                m_zoom = 1.0;
                m_pan = QPointF();
            }
        m_bounds = QRectF();
        for (const QPointF &p : std::as_const(m_points))
            {
                const QPointF q = projected(p);
                m_bounds = m_bounds.isNull() ? QRectF(q, QSizeF(1e-9, 1e-9)) : m_bounds.united(QRectF(q, QSizeF(1e-9, 1e-9)));
            }
        update();
    }

    void setSurfaces(const QVector<SurfaceStretch> &surfaces)
    {
        m_surfaces = surfaces;
        update();
    }

    // the one picked in the table is drawn broader; -1: none
    void setPickedSurface(int index)
    {
        m_pickedSurface = index;
        update();
    }

    // a stretch of time drawn broad and red along the route; none when `end` is not after `start`
    void setHighlight(double start, double end)
    {
        m_highlightStart = start;
        m_highlightEnd = end;
        update();
    }

    // where a stretch being marked begins; nowhere when `shown` is false
    void setMark(bool shown, double lat = 0.0, double lon = 0.0)
    {
        m_marked = shown;
        m_mark = QPointF(lon, lat);
        update();
    }

    // a street map under the route, with this credit in its corner
    void setStreets(bool shown, const QString &credit)
    {
        m_streets = shown;
        m_credit = credit;
        update();
    }

    void setTime(double time)
    {
        // asked a few times a second, also while the video stands still
        if (time == m_time)
            {
                return;
            }
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
                painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, QCoreApplication::translate("TrackView", "No positions in the data"));
                return;
            }
        const QTransform map = toWidget();
        bool streets = false;
        if (m_streets)
            {
                // what the widget shows, back on the map
                const TileStore::Picture picture = TileStore::instance()->picture(map.inverted().mapRect(QRectF(rect())));
                if (!picture.image.isNull())
                    {
                        painter.setRenderHint(QPainter::SmoothPixmapTransform);
                        painter.drawImage(map.mapRect(picture.bounds), picture.image);
                        streets = true;
                    }
            }
        QPainterPath route;
        route.moveTo(map.map(projected(m_points.first())));
        for (int i = 1; i < m_points.size(); ++i)
            {
                route.lineTo(map.map(projected(m_points.at(i))));
            }
        if (streets)
            {
                // seen on any map: a dark line with a light edge
                painter.setPen(QPen(QColor(255, 255, 255, 220), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(route);
                painter.setPen(QPen(QColor(20, 60, 200), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
        else
            {
                painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
            }
        painter.drawPath(route);

        // the surfaces marked: a broad band along the route, each kind in a color of its own
        for (int index = 0; index < m_surfaces.size(); ++index)
            {
                const SurfaceStretch &stretch = m_surfaces.at(index);
                QPainterPath band;
                bool begun = false;
                for (int i = 0; i < m_points.size(); ++i)
                    {
                        if (m_distances.at(i) < stretch.from || m_distances.at(i) > stretch.to)
                            {
                                continue;
                            }
                        const QPointF p = map.map(projected(m_points.at(i)));
                        begun ? band.lineTo(p) : band.moveTo(p);
                        begun = true;
                    }
                if (!begun)
                    {
                        continue;
                    }
                QColor color = QColor::fromHsv(int(qHash(stretch.surface) % 360), 220, 230);
                color.setAlpha(170);
                painter.setPen(QPen(color, index == m_pickedSurface ? 12 : 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(band);
                const QPointF middle = band.pointAtPercent(0.5);
                const QRectF label = painter.boundingRect(QRectF(middle + QPointF(8, -8), QSizeF(1, 1)),
                                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, stretch.surface);
                painter.fillRect(label.adjusted(-3, -1, 3, 1), QColor(255, 255, 255, 200));
                painter.setPen(QColor(30, 30, 30));
                painter.drawText(label, Qt::AlignCenter | Qt::TextDontClip, stretch.surface);
            }
        if (m_highlightEnd > m_highlightStart)
            {
                QPainterPath band;
                bool begun = false;
                for (int i = 0; i < m_points.size(); ++i)
                    {
                        if (m_times.at(i) >= m_highlightStart && m_times.at(i) <= m_highlightEnd)
                            {
                                const QPointF p = map.map(projected(m_points.at(i)));
                                begun ? band.lineTo(p) : band.moveTo(p);
                                begun = true;
                            }
                    }
                painter.setPen(QPen(QColor(230, 40, 40, 200), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(band);
            }
        if (m_marked)
            {
                painter.setPen(QPen(Qt::white, 2));
                painter.setBrush(QColor(230, 120, 20));
                painter.drawEllipse(map.map(projected(m_mark)), 6, 6);
            }

        for (const StartFinishLine &sector : m_sectors)
            {
                drawLine(painter, map, sector, QColor(40, 120, 220));
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
                painter.setPen(streets ? QPen(Qt::white, 2) : QPen(Qt::NoPen));
                painter.setBrush(palette().color(QPalette::Highlight));
                painter.drawEllipse(map.map(projected(QPointF(lon->valueAt(m_time), la))), 5, 5);
            }
        if (streets && !m_credit.isEmpty())
            {
                QFont small = font();
                small.setPointSizeF(small.pointSizeF() * 0.75);
                painter.setFont(small);
                const QFontMetricsF metrics(small);
                QRectF box(0, 0, std::min(metrics.horizontalAdvance(m_credit) + 8.0, width() - 4.0), metrics.height() + 2.0);
                box.moveBottomRight(QPointF(width() - 2.0, height() - 2.0));
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 190));
                painter.drawRoundedRect(box, 3, 3);
                painter.setPen(QColor(30, 30, 30));
                painter.drawText(box, Qt::AlignCenter, metrics.elidedText(m_credit, Qt::ElideLeft, box.width() - 6.0));
                painter.setFont(font());
            }
        if (!m_placing.isEmpty())
            {
                const QRect place = rect().adjusted(6, 6, -6, -6);
                const int flags = Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap;
                if (streets)
                    {
                        painter.fillRect(painter.boundingRect(place, flags, m_placing).adjusted(-4, -2, 4, 2), QColor(255, 255, 255, 210));
                    }
                painter.setPen(streets ? QColor(20, 60, 200) : palette().color(QPalette::Highlight));
                painter.drawText(place, flags, m_placing);
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            {
                m_pressedAt = event->position();
                m_lastAt = event->position();
                m_dragging = false;
            }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || m_points.size() < 2)
            {
                return;
            }
        // a few pixels are a shaky click, not a drag
        if (!m_dragging && (event->position() - m_pressedAt).manhattanLength() < 5)
            {
                return;
            }
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        const double scale = toWidget().m11();
        m_pan -= (event->position() - m_lastAt) / scale;
        m_lastAt = event->position();
        holdPan();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool dragged = m_dragging;
        m_dragging = false;
        setCursor(m_placing.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
        if (dragged || m_placing.isEmpty() || m_points.size() < 2 || event->button() != Qt::LeftButton
                || !clicked)
            {
                return;
            }
        const QPointF p = toWidget().inverted().map(event->position());
        clicked(TileStore::latitudeOf(-p.y()), p.x());
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double steps = event->angleDelta().y() / 120.0;
        if (steps != 0.0 && m_points.size() >= 2)
            {
                zoomAround(event->position(), std::pow(1.25, steps));
            }
        event->accept();
    }

    void resizeEvent(QResizeEvent *) override
    {
        m_zoomIn->move(6, 6);
        m_fit->move(6, 34);
        m_zoomOut->move(6, 62);
    }

private:
    // Closer by `factor`, the place under `at` staying where it is.
    void zoomAround(const QPointF &at, double factor)
    {
        const QPointF before = toWidget().inverted().map(at);
        m_zoom = std::clamp(m_zoom * factor, 1.0, 256.0);
        const QPointF after = toWidget().inverted().map(at);
        m_pan += before - after;
        holdPan();
        update();
    }

    // The middle of the view stays over the route.
    void holdPan()
    {
        const QPointF middle = m_bounds.center() + m_pan;
        const QPointF held(std::clamp(middle.x(), m_bounds.left(), m_bounds.right()),
                           std::clamp(middle.y(), m_bounds.top(), m_bounds.bottom()));
        m_pan = held - m_bounds.center();
    }

    // drawn long enough to be seen on a long route
    void drawLine(QPainter &painter, const QTransform &map, const StartFinishLine &line, const QColor &color) const
    {
        const QPointF a = map.map(projected(QPointF(line.lon1, line.lat1)));
        const QPointF b = map.map(projected(QPointF(line.lon2, line.lat2)));
        const QPointF middle = (a + b) / 2.0;
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const QPointF half = length > 0.0 ? (b - a) / length * std::max(length, 16.0) / 2.0 : QPointF();
        painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(middle - half, middle + half);
    }

    // degrees -> the plane of web maps, north up (see TileStore)
    QPointF projected(const QPointF &lonLat) const
    {
        return QPointF(lonLat.x(), -TileStore::mercatorLatitude(lonLat.y()));
    }

    QTransform toWidget() const
    {
        const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -12);
        const double scale = m_zoom * std::min(area.width() / std::max(m_bounds.width(), 1e-9),
                                               area.height() / std::max(m_bounds.height(), 1e-9));
        const QPointF middle = m_bounds.center() + m_pan;
        QTransform t;
        t.translate(area.center().x(), area.center().y());
        t.scale(scale, scale);
        t.translate(-middle.x(), -middle.y());
        return t;
    }

    TelemetrySessionPtr m_session;
    QRectF m_bounds;             // the route, projected
    double m_zoom{1.0};          // 1: the whole route
    QPointF m_pan;               // the middle of the view from that of the route, projected
    QPointF m_pressedAt;
    QPointF m_lastAt;
    bool m_dragging{false};
    QToolButton *m_zoomIn{nullptr};
    QToolButton *m_fit{nullptr};
    QToolButton *m_zoomOut{nullptr};
    StartFinishLine m_line;
    QVector<StartFinishLine> m_sectors;
    QVector<QPointF> m_points;   // lon, lat
    QVector<double> m_times;     // when the ride was at each of them, s
    QVector<double> m_distances; // how far into the ride each of them is, m
    double m_highlightStart{0.0};
    double m_highlightEnd{0.0};
    QVector<SurfaceStretch> m_surfaces;
    int m_pickedSurface{-1};
    bool m_marked{false};
    QPointF m_mark;              // lon, lat
    double m_time{0.0};
    bool m_streets{false};
    QString m_credit;
    QString m_placing;
};
