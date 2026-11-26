#include "gaugeoverlay.h"

#include "perfmeter.h"

#include <QPainter>

GaugeOverlay::GaugeOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // a picture of gauges is mostly see-through
    setOpaquePainting(false);
    setAntialiasing(true);
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

void GaugeOverlay::paint(QPainter *painter)
{
    if (!m_controller || width() <= 0.0 || height() <= 0.0)
        {
            return;
        }
    static PerfMeter meter("gauges painted");
    const PerfMeter::Scope timed(meter);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    m_controller->overlay()->render(*painter, QRectF(0.0, 0.0, width(), height()), m_controller->telemetryTime(m_position));
}
