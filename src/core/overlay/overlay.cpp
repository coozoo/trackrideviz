#include "overlay.h"

#include "../gauge/builtingauges.h"
#include "../gauge/gaugerenderer.h"

#include <cmath>

Overlay::Overlay(const QVector<OverlayItem> &items, const Units &units)
{
    for (const OverlayItem &item : items)
        {
            const Gauge gauge = BuiltinGauges::byId(item.gaugeId, units);
            if (!gauge.components.isEmpty() && !gauge.size.isEmpty())
                {
                    m_placed.append({item, gauge});
                }
        }
}

QVector<OverlayItem> Overlay::defaultItems()
{
    // a third of the frame height, with a small margin; x assumes 16:9
    OverlayItem speedometer;
    speedometer.gaugeId = BuiltinGauges::SpeedometerId;
    speedometer.height = 1.0 / 3.0;
    speedometer.x = 0.04 * 9.0 / 16.0;
    speedometer.y = 1.0 - 0.04 - speedometer.height;
    return {speedometer};
}

QRectF Overlay::itemRect(int index, const QRectF &frame) const
{
    const Placed &placed = m_placed.at(index);
    const double height = placed.item.height * frame.height();
    const double width = height * placed.gauge.size.width() / placed.gauge.size.height();
    return QRectF(frame.left() + placed.item.x * frame.width(),
                  frame.top() + placed.item.y * frame.height(), width, height);
}

QRect Overlay::boundingRect(const QSize &frameSize) const
{
    const QRectF frame(QPointF(0, 0), QSizeF(frameSize));
    QRectF all;
    for (int i = 0; i < m_placed.size(); ++i)
        {
            all = all.united(itemRect(i, frame));
        }
    all = all.intersected(frame);
    if (all.isEmpty())
        {
            return QRect();
        }
    // grow outwards to even coordinates
    const int left = int(std::floor(all.left() / 2.0)) * 2;
    const int top = int(std::floor(all.top() / 2.0)) * 2;
    const int right = std::min(int(std::ceil(all.right() / 2.0)) * 2, frameSize.width() / 2 * 2);
    const int bottom = std::min(int(std::ceil(all.bottom() / 2.0)) * 2, frameSize.height() / 2 * 2);
    return QRect(left, top, right - left, bottom - top);
}

void Overlay::render(QPainter &painter, const QRectF &frame, const TelemetrySession *session, double time) const
{
    for (int i = 0; i < m_placed.size(); ++i)
        {
            GaugeRenderer::render(painter, m_placed.at(i).gauge, itemRect(i, frame), session, time);
        }
}
