#pragma once

#include "../gauge/gaugerenderer.h"
#include "../gauge/gaugetheme.h"
#include "../telemetry/channelids.h"
#include "../telemetry/telemetrysession.h"
#include "../units/units.h"

#include <QRect>
#include <QVector>

class QPainter;

// Where one gauge sits on the picture and what it shows. Fractions of the
// frame, so the same placement works for the preview, a 720p export and a
// 4K one.
struct OverlayItem
{
    QString gaugeId;                        // "builtin:speedometer", "library:<folder>"
    QString channel{ChannelId::Speed};      // the measurement the gauge is bound to
    double x{0.0};        // left edge, fraction of the frame width
    double y{0.0};        // top edge, fraction of the frame height
    double height{0.25};  // fraction of the frame height; the width follows the gauge's shape

    bool operator==(const OverlayItem &other) const
    {
        return gaugeId == other.gaugeId && channel == other.channel
               && x == other.x && y == other.y && height == other.height;
    }
    bool operator!=(const OverlayItem &other) const { return !(*this == other); }
};

// Everything drawn on top of the video, ready to render.
//
// Built once on the GUI thread (gauges are looked up in the library, bound
// to their channels, coloured for the theme), then only read: the preview
// and an export worker can share one instance. One render() for every
// consumer - what is seen while editing is what gets exported.
class Overlay
{
public:
    Overlay() = default;
    // Items whose gauge is not in the library are kept as empty places, so
    // that indexes here match the project's list.
    Overlay(const QVector<OverlayItem> &items, const TelemetrySessionPtr &session,
            const Units &units, const GaugeTheme &theme);

    // True when nothing would be drawn.
    bool isEmpty() const;
    int count() const { return m_placed.size(); }
    // False for an item whose gauge could not be found.
    bool hasGauge(int index) const { return !m_placed.at(index).gauge.isEmpty(); }

    // What a new project starts with: the dial in the lower left corner, showing speed.
    static QVector<OverlayItem> defaultItems();

    // Rectangle of item `index` for a frame occupying `frame`. A missing
    // gauge is taken as square.
    QRectF itemRect(int index, const QRectF &frame) const;
    // Topmost item under `point`, or -1.
    int itemAt(const QPointF &point, const QRectF &frame) const;

    const OverlayItem &item(int index) const { return m_placed.at(index).item; }
    // The same overlay with one gauge somewhere else. Cheap (nothing is
    // bound again), so it can follow the mouse while a gauge is dragged.
    Overlay withGeometry(int index, double x, double y, double height) const;

    // Smallest pixel rectangle that holds every gauge on a frame of this
    // size, clipped to the frame, with even position and size (video chroma
    // is subsampled by two). Empty when there is nothing to draw.
    QRect boundingRect(const QSize &frameSize) const;

    // The same, but one rectangle per group of gauges that touch each other:
    // gauges in opposite corners give two small rectangles, not the whole
    // frame. At most `maxRegions`; past that the closest ones are joined.
    QVector<QRect> regions(const QSize &frameSize, int maxRegions = 8) const;

    // Draws all gauges for telemetry time `time` on a frame occupying `frame`
    // in painter coordinates.
    void render(QPainter &painter, const QRectF &frame, double time) const;
    // Only the gauges that reach into `within` (painter coordinates).
    void render(QPainter &painter, const QRectF &frame, double time, const QRectF &within) const;

private:
    struct Placed
    {
        OverlayItem item;
        BoundGauge gauge;
    };
    QVector<Placed> m_placed;
};
