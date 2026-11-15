#pragma once

#include "laps/lapengine.h"
#include "maps/tilestore.h"
#include "telemetry/channelids.h"
#include "telemetry/surfaces.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

// The route from above, north up, fitted into the widget.
class TrackView : public QWidget
{
public:
    explicit TrackView(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(120, 80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    // A click on the map: latitude and longitude. Called while something is being placed
    // (setPlacing), or always with `clicksAlways`.
    std::function<void(double, double)> clicked;
    bool clicksAlways{false};

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors)
    {
        m_session = session;
        m_line = line;
        m_sectors = sectors;
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
        if ((m_placing.isEmpty() && !clicksAlways) || m_points.size() < 2 || event->button() != Qt::LeftButton || !clicked)
            {
                return;
            }
        const QPointF p = toWidget().inverted().map(event->position());
        clicked(TileStore::latitudeOf(-p.y()), p.x());
    }

private:
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
