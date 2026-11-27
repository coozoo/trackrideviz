#pragma once

#include "maps/flyover.h"

#include <QRectF>
#include <QSGNode>

#include <memory>

class QQuickWindow;
class QSGClipNode;
class QSGGeometryNode;
class FlyoverLandMaterial;

// A flyover's land drawn by the graphics card in a rectangle of an item (GaugeOverlay): the sky,
// then the land's triangles the farthest first, inside the gauge's rounded corners. What the
// software picture draws (Flyover::render), drawn as fast as the card draws.
class FlyoverNode : public QSGNode
{
public:
    FlyoverNode();

    // `rect` and `radius` in the item's coordinates.
    void update(QQuickWindow *window, const Flyover::Scene &scene, const QRectF &rect, double radius);

    // Whether the graphics card of `window` can draw it (not with the software scene graph).
    static bool available(QQuickWindow *window);

private:
    QSGClipNode *m_clip{nullptr};
    QSGGeometryNode *m_sky{nullptr};
    QSGGeometryNode *m_land{nullptr};
    FlyoverLandMaterial *m_material{nullptr};
    std::shared_ptr<const Flyover::Land> m_shown;   // whose points the geometry has
    QRectF m_rect;
    double m_radius{-1.0};
};
