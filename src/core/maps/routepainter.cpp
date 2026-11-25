#include "routepainter.h"

#include "tilestore.h"
#include "../telemetry/channelids.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

void RoutePainter::setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
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
            fit();
        }
    m_bounds = QRectF();
    for (const QPointF &p : std::as_const(m_points))
        {
            const QPointF q = projected(p);
            m_bounds = m_bounds.isNull() ? QRectF(q, QSizeF(1e-9, 1e-9)) : m_bounds.united(QRectF(q, QSizeF(1e-9, 1e-9)));
        }
}

bool RoutePainter::setTime(double time)
{
    if (time == m_time)
        {
            return false;
        }
    m_time = time;
    return true;
}

void RoutePainter::paint(QPainter &painter, const QRectF &area, const Colors &colors, const QFont &font,
                         const QString &hint) const
{
    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font);
    painter.fillRect(area, colors.base);
    if (!hasRoute())
        {
            painter.setPen(colors.placeholder);
            painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
                             QCoreApplication::translate("TrackView", "No positions in the data"));
            painter.restore();
            return;
        }
    const QTransform map = toView(area);
    bool streets = false;
    if (m_streets)
        {
            // what the view shows, back on the map
            const TileStore::Picture picture = TileStore::instance()->picture(map.inverted().mapRect(area));
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
            painter.setPen(QPen(colors.text, 1.5));
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
    const double la = lat ? lat->valueAt(m_time, &ok) : 0.0;
    if (ok && lon)
        {
            painter.setPen(streets ? QPen(Qt::white, 2) : QPen(Qt::NoPen));
            painter.setBrush(colors.highlight);
            painter.drawEllipse(map.map(projected(QPointF(lon->valueAt(m_time), la))), 5, 5);
        }
    if (streets && !m_credit.isEmpty())
        {
            QFont small = font;
            small.setPointSizeF(small.pointSizeF() * 0.75);
            painter.setFont(small);
            const QFontMetricsF metrics(small);
            QRectF box(0, 0, std::min(metrics.horizontalAdvance(m_credit) + 8.0, area.width() - 4.0), metrics.height() + 2.0);
            box.moveBottomRight(area.bottomRight() - QPointF(2.0, 2.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 190));
            painter.drawRoundedRect(box, 3, 3);
            painter.setPen(QColor(30, 30, 30));
            painter.drawText(box, Qt::AlignCenter, metrics.elidedText(m_credit, Qt::ElideLeft, box.width() - 6.0));
            painter.setFont(font);
        }
    if (!hint.isEmpty())
        {
            const QRectF place = area.adjusted(6, 6, -6, -6);
            const int flags = Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap;
            if (streets)
                {
                    painter.fillRect(painter.boundingRect(place, flags, hint).adjusted(-4, -2, 4, 2), QColor(255, 255, 255, 210));
                }
            painter.setPen(streets ? QColor(20, 60, 200) : colors.highlight);
            painter.drawText(place, flags, hint);
        }
    painter.restore();
}

void RoutePainter::zoomAround(const QPointF &at, double factor, const QRectF &area)
{
    if (!hasRoute())
        {
            return;
        }
    const QPointF before = toView(area).inverted().map(at);
    m_zoom = std::clamp(m_zoom * factor, 1.0, 256.0);
    const QPointF after = toView(area).inverted().map(at);
    m_pan += before - after;
    holdPan();
}

void RoutePainter::moveBy(const QPointF &pixels, const QRectF &area)
{
    if (!hasRoute())
        {
            return;
        }
    m_pan -= pixels / toView(area).m11();
    holdPan();
}

void RoutePainter::fit()
{
    m_zoom = 1.0;
    m_pan = QPointF();
}

QPointF RoutePainter::placeAt(const QPointF &at, const QRectF &area) const
{
    const QPointF p = toView(area).inverted().map(at);
    return QPointF(p.x(), TileStore::latitudeOf(-p.y()));
}

void RoutePainter::holdPan()
{
    // the middle of the view stays over the route
    const QPointF middle = m_bounds.center() + m_pan;
    const QPointF held(std::clamp(middle.x(), m_bounds.left(), m_bounds.right()),
                       std::clamp(middle.y(), m_bounds.top(), m_bounds.bottom()));
    m_pan = held - m_bounds.center();
}

// drawn long enough to be seen on a long route
void RoutePainter::drawLine(QPainter &painter, const QTransform &map, const StartFinishLine &line, const QColor &color) const
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
QPointF RoutePainter::projected(const QPointF &lonLat) const
{
    return QPointF(lonLat.x(), -TileStore::mercatorLatitude(lonLat.y()));
}

QTransform RoutePainter::toView(const QRectF &area) const
{
    const QRectF inner = area.adjusted(12, 12, -12, -12);
    const double scale = m_zoom * std::min(inner.width() / std::max(m_bounds.width(), 1e-9),
                                           inner.height() / std::max(m_bounds.height(), 1e-9));
    const QPointF middle = m_bounds.center() + m_pan;
    QTransform t;
    t.translate(inner.center().x(), inner.center().y());
    t.scale(scale, scale);
    t.translate(-middle.x(), -middle.y());
    return t;
}
