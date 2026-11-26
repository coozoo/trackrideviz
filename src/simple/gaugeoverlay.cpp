#include "gaugeoverlay.h"

#include "perfmeter.h"

#include <QPainter>

namespace
{
#ifdef Q_OS_ANDROID
// A phone's screen has about three pixels to a point, and the gauges are drawn by the processor:
// drawn at 1.5 and stretched, they take about a quarter of the time and stay sharp enough.
const qreal kMostDensity = 1.5;
#else
const qreal kMostDensity = 0.0;   // as the screen has it
#endif
}

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
    const double time = m_controller->telemetryTime(m_position);
    const qreal density = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    if (kMostDensity > 0.0 && density > kMostDensity)
        {
            const QSize pixels = (size() * kMostDensity).toSize();
            if (m_picture.size() != pixels)
                {
                    m_picture = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
                    m_picture.setDevicePixelRatio(kMostDensity);
                }
            m_picture.fill(Qt::transparent);
            QPainter picture(&m_picture);
            picture.setRenderHint(QPainter::Antialiasing);
            picture.setRenderHint(QPainter::SmoothPixmapTransform);
            m_controller->overlay()->render(picture, QRectF(0.0, 0.0, width(), height()), time);
            picture.end();
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawImage(QRectF(0.0, 0.0, width(), height()), m_picture);
            return;
        }
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    m_controller->overlay()->render(*painter, QRectF(0.0, 0.0, width(), height()), time);
}
