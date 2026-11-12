#pragma once

#include "../gauge/gauge.h"
#include "../units/units.h"

#include <QRect>
#include <QVector>

class QPainter;
class TelemetrySession;

// Where one gauge sits on the picture. Fractions of the frame, so the same
// placement works for the preview, a 720p export and a 4K one.
struct OverlayItem
{
    QString gaugeId;      // e.g. "builtin:speedometer"
    double x{0.0};        // left edge, fraction of the frame width
    double y{0.0};        // top edge, fraction of the frame height
    double height{0.25};  // fraction of the frame height; the width follows the gauge's shape

    bool operator==(const OverlayItem &other) const
    {
        return gaugeId == other.gaugeId && x == other.x && y == other.y && height == other.height;
    }
};

// Everything drawn on top of the video, ready to render.
//
// Built once on the GUI thread, then only read: the preview and an export
// worker can share one instance (the gauges' images are implicitly shared).
// One render() for every consumer - what is seen while editing is what gets
// exported.
class Overlay
{
public:
    Overlay() = default;
    // Items whose gauge id is unknown are left out.
    Overlay(const QVector<OverlayItem> &items, const Units &units);

    bool isEmpty() const { return m_placed.isEmpty(); }
    int count() const { return m_placed.size(); }

    // What a new project starts with: the speedometer in the lower left corner.
    static QVector<OverlayItem> defaultItems();

    // Rectangle of item `index` for a frame occupying `frame`.
    QRectF itemRect(int index, const QRectF &frame) const;

    // Smallest pixel rectangle that holds every gauge on a frame of this
    // size, clipped to the frame, with even position and size (video chroma
    // is subsampled by two). Empty when there is nothing to draw.
    QRect boundingRect(const QSize &frameSize) const;

    // Draws all gauges for telemetry time `time` on a frame occupying `frame`
    // in painter coordinates. session may be nullptr.
    void render(QPainter &painter, const QRectF &frame, const TelemetrySession *session, double time) const;

private:
    struct Placed
    {
        OverlayItem item;
        Gauge gauge;
    };
    QVector<Placed> m_placed;
};
