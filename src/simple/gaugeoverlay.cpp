#include "gaugeoverlay.h"

#include "perfmeter.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>

#include <algorithm>

namespace
{
#ifdef Q_OS_ANDROID
// A phone's screen has about three pixels to a point, and the gauges are drawn by the processor:
// drawn at 1.5 and stretched by the graphics card, they take about a quarter of the time and
// stay sharp enough.
const qreal kMostDensity = 1.5;
#else
const qreal kMostDensity = 0.0;   // as the screen has it
#endif
}

GaugeOverlay::GaugeOverlay(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void GaugeOverlay::setController(SimpleController *controller)
{
    if (controller == m_controller)
        {
            return;
        }
    if (m_controller)
        {
            disconnect(m_controller, nullptr, this, nullptr);
        }
    m_controller = controller;
    if (m_controller)
        {
            // other gauges, another template, the data moved: drawn again
            connect(m_controller, &SimpleController::overlayChanged, this, [this]() { update(); });
        }
    emit controllerChanged();
    update();
}

void GaugeOverlay::setPosition(qint64 positionMs)
{
    if (positionMs == m_position)
        {
            return;
        }
    m_position = positionMs;
    emit positionChanged();
    update();
}

QSGNode *GaugeOverlay::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGImageNode *>(old);
    if (!m_controller || !window() || width() <= 0.0 || height() <= 0.0)
        {
            delete node;
            return nullptr;
        }
    static PerfMeter meter("gauges painted");
    const PerfMeter::Scope timed(meter);
    const qreal screen = window()->effectiveDevicePixelRatio();
    const qreal density = kMostDensity > 0.0 ? std::min(screen, kMostDensity) : screen;
    // a picture of gauges is mostly see-through
    QImage picture((size() * density).toSize(), QImage::Format_ARGB32_Premultiplied);
    picture.setDevicePixelRatio(density);
    picture.fill(Qt::transparent);
    {
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        m_controller->overlay()->render(painter, QRectF(0.0, 0.0, width(), height()), m_controller->telemetryTime(m_position));
    }
    if (!node)
        {
            node = window()->createImageNode();
            node->setOwnsTexture(true);
            node->setFiltering(QSGTexture::Linear);
        }
    node->setTexture(window()->createTextureFromImage(picture));
    node->setRect(boundingRect());
    return node;
}
