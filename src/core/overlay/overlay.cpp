#include "overlay.h"

#include "../gauge/builtingauges.h"
#include "../gauge/gaugelibrary.h"

#include <algorithm>
#include <cmath>

Overlay::Overlay(const QVector<OverlayItem> &items, const TelemetrySessionPtr &session,
                 const Units &units, const GaugeTheme &theme)
{
    for (const OverlayItem &item : items)
        {
            const Gauge gauge = GaugeLibrary::gauge(item.gaugeId);
            m_placed.append({item, gauge.isEmpty() ? BoundGauge() : BoundGauge(gauge, item.channel, session, units, theme)});
        }
}

bool Overlay::isEmpty() const
{
    for (const Placed &placed : m_placed)
        {
            if (!placed.gauge.isEmpty())
                {
                    return false;
                }
        }
    return true;
}

QVector<OverlayItem> Overlay::defaultItems()
{
    // a third of the frame height, with a small margin; x assumes 16:9
    OverlayItem dial;
    dial.gaugeId = BuiltinGauges::DialId;
    dial.height = 1.0 / 3.0;
    dial.x = 0.04 * 9.0 / 16.0;
    dial.y = 1.0 - 0.04 - dial.height;
    return {dial};
}

QRectF Overlay::itemRect(int index, const QRectF &frame) const
{
    const Placed &placed = m_placed.at(index);
    const QSizeF shape = placed.gauge.isEmpty() ? QSizeF(1, 1) : placed.gauge.size();
    const double height = placed.item.height * frame.height();
    const double width = height * shape.width() / shape.height();
    return QRectF(frame.left() + placed.item.x * frame.width(),
                  frame.top() + placed.item.y * frame.height(), width, height);
}

int Overlay::itemAt(const QPointF &point, const QRectF &frame) const
{
    // later items are drawn on top
    for (int i = m_placed.size() - 1; i >= 0; --i)
        {
            if (itemRect(i, frame).contains(point))
                {
                    return i;
                }
        }
    return -1;
}

Overlay Overlay::withGeometry(int index, double x, double y, double height) const
{
    Overlay copy = *this;
    OverlayItem &item = copy.m_placed[index].item;
    item.x = x;
    item.y = y;
    item.height = height;
    return copy;
}

namespace
{
// Grown outwards to even coordinates and kept inside the frame.
QRect evenRect(const QRectF &rect, const QSize &frameSize)
{
    const QRectF inside = rect.intersected(QRectF(QPointF(0, 0), QSizeF(frameSize)));
    if (inside.isEmpty())
        {
            return QRect();
        }
    const int left = int(std::floor(inside.left() / 2.0)) * 2;
    const int top = int(std::floor(inside.top() / 2.0)) * 2;
    const int right = std::min(int(std::ceil(inside.right() / 2.0)) * 2, frameSize.width() / 2 * 2);
    const int bottom = std::min(int(std::ceil(inside.bottom() / 2.0)) * 2, frameSize.height() / 2 * 2);
    return QRect(left, top, right - left, bottom - top);
}

qint64 area(const QRect &rect)
{
    return qint64(rect.width()) * rect.height();
}
}

QVector<QRect> Overlay::regions(const QSize &frameSize, int maxRegions) const
{
    const QRectF frame(QPointF(0, 0), QSizeF(frameSize));
    QVector<QRect> rects;
    for (int i = 0; i < m_placed.size(); ++i)
        {
            if (!m_placed.at(i).gauge.isEmpty())
                {
                    const QRect rect = evenRect(itemRect(i, frame), frameSize);
                    if (!rect.isEmpty())
                        {
                            rects.append(rect);
                        }
                }
        }
    // gauges that overlap or touch are one region; joining two can make it
    // touch a third, so repeat until nothing changes
    for (bool joined = true; joined;)
        {
            joined = false;
            for (int a = 0; a < rects.size() && !joined; ++a)
                {
                    for (int b = a + 1; b < rects.size() && !joined; ++b)
                        {
                            if (rects.at(a).adjusted(-2, -2, 2, 2).intersects(rects.at(b)))
                                {
                                    rects[a] = rects.at(a).united(rects.at(b));
                                    rects.removeAt(b);
                                    joined = true;
                                }
                        }
                }
        }
    // too many: join the pair that adds the least empty space, until few enough
    while (rects.size() > std::max(1, maxRegions))
        {
            int bestA = 0;
            int bestB = 1;
            qint64 bestCost = -1;
            for (int a = 0; a < rects.size(); ++a)
                {
                    for (int b = a + 1; b < rects.size(); ++b)
                        {
                            const qint64 cost = area(rects.at(a).united(rects.at(b))) - area(rects.at(a)) - area(rects.at(b));
                            if (bestCost < 0 || cost < bestCost)
                                {
                                    bestCost = cost;
                                    bestA = a;
                                    bestB = b;
                                }
                        }
                }
            rects[bestA] = rects.at(bestA).united(rects.at(bestB));
            rects.removeAt(bestB);
        }
    // a joined region may now overlap another; that only means some gauge
    // pixels are sent twice and laid on top of themselves - drawn identically
    std::sort(rects.begin(), rects.end(), [](const QRect &a, const QRect &b)
    {
        return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x();
    });
    return rects;
}

QRect Overlay::boundingRect(const QSize &frameSize) const
{
    const QRectF frame(QPointF(0, 0), QSizeF(frameSize));
    QRectF all;
    for (int i = 0; i < m_placed.size(); ++i)
        {
            if (!m_placed.at(i).gauge.isEmpty())
                {
                    all = all.united(itemRect(i, frame));
                }
        }
    return evenRect(all, frameSize);
}

void Overlay::render(QPainter &painter, const QRectF &frame, double time) const
{
    for (int i = 0; i < m_placed.size(); ++i)
        {
            m_placed.at(i).gauge.render(painter, itemRect(i, frame), time);
        }
}

void Overlay::render(QPainter &painter, const QRectF &frame, double time, const QRectF &within) const
{
    for (int i = 0; i < m_placed.size(); ++i)
        {
            const QRectF rect = itemRect(i, frame);
            if (rect.intersects(within))
                {
                    m_placed.at(i).gauge.render(painter, rect, time);
                }
        }
}
