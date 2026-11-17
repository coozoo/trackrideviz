#pragma once

#include "../gauge/gaugerenderer.h"
#include "../gauge/gaugetheme.h"
#include "../telemetry/channelids.h"
#include "../telemetry/telemetrysession.h"
#include "../units/units.h"

#include <QHash>
#include <QJsonObject>
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
    double height{0.25};  // fraction of the frame height; the width follows the gauge's shape...
    // ...or, when above 0, this shape: width / height on the picture, the gauge laid out again
    // for it (GaugeShape)
    double aspect{0.0};
    // A street map under the gauge's route: as the gauge was drawn, or the user's choice.
    enum Streets { AsDesigned, Shown, Hidden };
    Streets streets{AsDesigned};
    // Its route close up, the map moving under the rider: meters across;
    // 0 = the whole route, negative = as the gauge was drawn.
    double closeUp{-1.0};
    // The close-up turned so that the way ahead points up: 1, north up: 0, as the gauge was drawn: -1.
    int headingUp{-1};
    // The units this gauge shows its values in: those of the settings, or its own.
    enum UnitSystem { AsSettings, MetricUnits, ImperialUnits };
    UnitSystem units{AsSettings};
    // Parts that follow a channel of their own showing another one instead: own -> shown.
    QHash<QString, QString> channels;
    // Where the scale of `channel` ends, in the gauge's units; 0 = as the data or the gauge has it.
    double scaleMax{0.0};

    // The units to draw the gauge with, `usual` being those of the settings.
    Units unitsFor(const Units &usual) const
    {
        if (units == AsSettings)
            {
                return usual;
            }
        Units own;
        own.speed = units == ImperialUnits ? Units::MilesPerHour : Units::KilometersPerHour;
        own.length = units == ImperialUnits ? Units::Imperial : Units::Metric;
        own.temperature = units == ImperialUnits ? Units::Fahrenheit : Units::Celsius;
        return own;
    }

    // `channels`, `scaleMax` and `aspect` in a project's or a template's entry of the item (nothing when unchanged).
    void writeChoices(QJsonObject &object) const;
    void readChoices(const QJsonObject &object);

    bool operator==(const OverlayItem &other) const
    {
        return gaugeId == other.gaugeId && channel == other.channel
               && x == other.x && y == other.y && height == other.height && aspect == other.aspect && streets == other.streets
               && closeUp == other.closeUp && headingUp == other.headingUp && units == other.units
               && channels == other.channels && scaleMax == other.scaleMax;
    }
    bool operator!=(const OverlayItem &other) const { return !(*this == other); }
};

// Everything drawn on top of the video, ready to render.
//
// Built once on the GUI thread (gauges are looked up in the library, bound
// to their channels, colored for the theme), then only read: the preview
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

    // True when the item's gauge draws a route, with or without a street map under it.
    bool hasRoute(int index) const { return m_placed.at(index).route; }
    bool hasStreets(int index) const { return m_placed.at(index).streets; }
    // Meters its route shows around the rider; 0 = the whole route.
    double closeUp(int index) const { return m_placed.at(index).closeUp; }
    bool headingUp(int index) const { return m_placed.at(index).headingUp; }

    // What a new project starts with: the dial in the lower left corner, showing speed.
    static QVector<OverlayItem> defaultItems();

    // Rectangle of item `index` for a frame occupying `frame`. A missing
    // gauge is taken as square.
    QRectF itemRect(int index, const QRectF &frame) const;
    // Topmost item under `point`, or -1.
    int itemAt(const QPointF &point, const QRectF &frame) const;

    const OverlayItem &item(int index) const { return m_placed.at(index).item; }
    // The same overlay with one gauge somewhere else. Cheap (nothing is
    // bound again), so it can follow the mouse while a gauge is dragged; a
    // new shape is drawn stretched until the overlay is built again.
    Overlay withGeometry(int index, double x, double y, double height, double aspect) const;

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
        bool route{false};
        bool streets{false};
        double closeUp{0.0};
        bool headingUp{false};
    };
    QVector<Placed> m_placed;
};
