#include "overlay.h"

#include "../gauge/builtingauges.h"
#include "../gauge/gaugelibrary.h"
#include "../gauge/gaugeshape.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
// The item's choices for its map (streets under it, close up), put into the gauge's routes;
// says what it found.
void chooseMap(QVector<GaugeComponent> &components, const OverlayItem &item, bool *route, bool *streets, double *closeUp,
               bool *headingUp, bool *flyover, GaugeComponent::FlyoverCamera *camera)
{
    const OverlayItem::Streets choice = item.streets;
    for (GaugeComponent &component : components)
        {
            if (component.type == GaugeComponent::TrackMap || component.type == GaugeComponent::LapMap)
                {
                    if (item.closeUp >= 0.0)
                        {
                            component.span = item.closeUp;
                            component.speedZoom = item.speedZoom;
                        }
                    if (item.headingUp >= 0)
                        {
                            component.headingUp = item.headingUp > 0;
                        }
                    if (item.flyover >= 0)
                        {
                            component.flyover = item.flyover > 0;
                        }
                    if (item.flyoverCamera.isSet())
                        {
                            component.flyoverCamera = item.flyoverCamera;
                        }
                    if (!item.map.isEmpty())
                        {
                            component.mapProvider = item.map;
                        }
                    *closeUp = component.span;
                    *headingUp = component.headingUp;
                    *flyover = component.flyover;
                    *camera = component.flyoverCamera;
                    if (choice == OverlayItem::Shown && !component.tiles)
                        {
                            // a gauge drawn without a map in mind: dimmed, corners taken off
                            component.tiles = true;
                            component.tileOpacity = 0.75;
                            if (component.cornerRadius <= 0.0)
                                {
                                    component.cornerRadius = std::min(component.frame.width(), component.frame.height()) * 0.07;
                                }
                        }
                    else if (choice == OverlayItem::Hidden)
                        {
                            component.tiles = false;
                        }
                    *route = true;
                    *streets = *streets || component.tiles;
                }
            chooseMap(component.children, item, route, streets, closeUp, headingUp, flyover, camera);
        }
}
}

Overlay::Overlay(const QVector<OverlayItem> &items, const TelemetrySessionPtr &session,
                 const Units &units, const GaugeTheme &theme)
{
    for (const OverlayItem &item : items)
        {
            Gauge gauge = GaugeShape::textsScaled(GaugeShape::widened(GaugeLibrary::gauge(item.gaugeId), item.aspect),
                                                  item.textScale);
            Placed placed;
            placed.item = item;
            chooseMap(gauge.components, item, &placed.route, &placed.streets, &placed.closeUp, &placed.headingUp, &placed.flyover,
                      &placed.flyoverCamera);
            if (!gauge.isEmpty())
                {
                    placed.gauge = BoundGauge(gauge, item.channel, session, item.unitsFor(units), item.ownTheme ? item.theme : theme,
                                              {item.channels, item.scaleMax});
                }
            m_placed.append(placed);
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
    const double width = height * (placed.item.aspect > 0.0 ? placed.item.aspect : shape.width() / shape.height());
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

Overlay Overlay::withGeometry(int index, double x, double y, double height, double aspect) const
{
    Overlay copy = *this;
    OverlayItem &item = copy.m_placed[index].item;
    item.x = x;
    item.y = y;
    item.height = height;
    item.aspect = aspect;
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
    // every gauge, wherever it is
    render(painter, frame, time, QRectF(-1e9, -1e9, 2e9, 2e9));
}

void Overlay::render(QPainter &painter, const QRectF &frame, double time, const QRectF &within) const
{
    for (int i = 0; i < m_placed.size(); ++i)
        {
            const QRectF rect = itemRect(i, frame);
            if (rect.intersects(within))
                {
                    const double opacity = std::clamp(m_placed.at(i).item.opacity, 0.0, 1.0);
                    if (opacity >= 1.0)
                        {
                            m_placed.at(i).gauge.render(painter, rect, time);
                            continue;
                        }
                    // drawn whole first, then faded as one: its parts do not show through each other
                    const QRect box = rect.toAlignedRect();
                    const qreal ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
                    QImage picture(box.size() * ratio, QImage::Format_ARGB32_Premultiplied);
                    picture.setDevicePixelRatio(ratio);
                    picture.fill(Qt::transparent);
                    {
                        QPainter inside(&picture);
                        m_placed.at(i).gauge.render(inside, rect.translated(-box.topLeft()), time);
                    }
                    painter.save();
                    painter.setOpacity(painter.opacity() * opacity);
                    painter.drawImage(box.topLeft(), picture);
                    painter.restore();
                }
        }
}

void OverlayItem::writeChoices(QJsonObject &object) const
{
    if (!channels.isEmpty())
        {
            QJsonObject shown;
            for (auto it = channels.cbegin(); it != channels.cend(); ++it)
                {
                    shown.insert(it.key(), it.value());
                }
            object.insert("channels", shown);
        }
    if (scaleMax > 0.0)
        {
            object.insert("scaleMax", scaleMax);
        }
    if (aspect > 0.0)
        {
            object.insert("aspect", aspect);
        }
    if (!map.isEmpty())
        {
            object.insert("map", map);
        }
    if (ownTheme)
        {
            object.insert("ownTheme", theme.toJson());
        }
    if (textScale != 1.0)
        {
            object.insert("textScale", textScale);
        }
    if (opacity < 1.0)
        {
            object.insert("opacity", opacity);
        }
}

void OverlayItem::readChoices(const QJsonObject &object)
{
    channels.clear();
    const QJsonObject shown = object.value("channels").toObject();
    for (auto it = shown.constBegin(); it != shown.constEnd(); ++it)
        {
            const QString channel = it.value().toString();
            if (!channel.isEmpty() && channel != it.key())
                {
                    channels.insert(it.key(), channel);
                }
        }
    scaleMax = std::max(0.0, object.value("scaleMax").toDouble());
    aspect = std::max(0.0, object.value("aspect").toDouble());
    map = object.value("map").toString();
    ownTheme = object.value("ownTheme").isObject();
    theme = ownTheme ? GaugeTheme::fromJson(object.value("ownTheme").toObject()) : GaugeTheme();
    textScale = std::clamp(object.value("textScale").toDouble(1.0), 0.2, 5.0);
    opacity = std::clamp(object.value("opacity").toDouble(1.0), 0.0, 1.0);
}
