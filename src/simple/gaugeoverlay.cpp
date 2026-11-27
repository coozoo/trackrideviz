#include "gaugeoverlay.h"

#include "flyovernode.h"
#include "maps/flyover.h"
#include "perfmeter.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>

#include <algorithm>
#include <optional>

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
    // the flyovers' land (FlyoverNode), then the picture of the gauges over it, last
    QSGNode *root = old;
    if (!m_controller || !window() || width() <= 0.0 || height() <= 0.0)
        {
            delete root;
            return nullptr;
        }
    static PerfMeter meter("gauges painted");
    const PerfMeter::Scope timed(meter);
    const qreal screen = window()->effectiveDevicePixelRatio();
    const qreal density = kMostDensity > 0.0 ? std::min(screen, kMostDensity) : screen;
    // a picture of gauges is mostly see-through; in the order of bytes the graphics card takes,
    // so that it is not converted on the way there
    QImage picture((size() * density).toSize(), QImage::Format_RGBA8888_Premultiplied);
    picture.setDevicePixelRatio(density);
    picture.fill(Qt::transparent);
    // a flyover's land on the graphics card where it can: the shader packs need Qt 6.4
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    const bool onCard = FlyoverNode::available(window());
#else
    const bool onCard = false;
#endif
    QVector<Flyover::Capture::Item> flyovers;
    {
        // a flyover's next land is made in the background: the preview goes on meanwhile
        const Flyover::Patience patience;
        std::optional<Flyover::Capture> capture;
        if (onCard)
            {
                capture.emplace();
            }
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        m_controller->overlay()->render(painter, QRectF(0.0, 0.0, width(), height()), m_controller->telemetryTime(m_position));
        painter.end();
        if (capture)
            {
                flyovers = capture->items;
            }
    }
    if (!root)
        {
            root = new QSGNode;
            QSGImageNode *image = window()->createImageNode();
            image->setOwnsTexture(true);
            image->setFiltering(QSGTexture::Linear);
            root->appendChildNode(image);
        }
    auto *image = static_cast<QSGImageNode *>(root->lastChild());
    image->setTexture(window()->createTextureFromImage(picture));
    image->setRect(boundingRect());
    // one FlyoverNode for each flyover, before the picture; the picture's pixels to the item's
    while (root->childCount() - 1 > flyovers.size())
        {
            QSGNode *gone = root->firstChild();
            root->removeChildNode(gone);
            delete gone;
        }
    while (root->childCount() - 1 < flyovers.size())
        {
            root->prependChildNode(new FlyoverNode);
        }
    QSGNode *child = root->firstChild();
    for (const Flyover::Capture::Item &flyover : std::as_const(flyovers))
        {
            const QRectF rect(flyover.rect.topLeft() / density, flyover.rect.size() / density);
            static_cast<FlyoverNode *>(child)->update(window(), flyover.scene, rect, flyover.radius / density);
            child = child->nextSibling();
        }
    return root;
}
