#pragma once

#include "../telemetry/telemetrysession.h"
#include "../units/units.h"
#include "gauge.h"
#include "gaugetheme.h"
#include "metricrange.h"

#include <QHash>
#include <QMutex>
#include <QPainterPath>

#include <memory>

class Flyover;
class QPainter;

// A gauge bound to a channel of a recording and ready to be drawn.
//
// Binding does everything that does not change from frame to frame: images
// are colored for the theme, every channel the gauge uses gets its unit,
// label and scale, interval markings get their numbers, graphs and route maps
// get their lines, animations their running phase. render() then only
// samples and paints.
//
// This is the one render path: the preview canvas, the export worker and the
// library thumbnails all draw through it, so what is seen while editing is
// what gets exported. A bound gauge is immutable; render() may be called
// from any thread (it needs a QGuiApplication for text).
//
// What does not move with the data (dial faces, scales, labels) is drawn once
// per size into a picture and copied from then on; only needles, values,
// fills and markers are drawn for every frame. Gauges are placed on whole
// pixels so the two always line up.
class BoundGauge
{
public:
    // What the user changed of a gauge as it was designed.
    struct Choices
    {
        // a part's own channel -> the one it shows instead
        QHash<QString, QString> channels;
        // the scale of the bound channel ends here (display units); 0 = as the data or the design has it
        double scaleMax{0.0};
    };

    BoundGauge() = default;
    // session may be null: every channel is then unknown and scales are 0..100.
    BoundGauge(const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &session,
               const Units &units, const GaugeTheme &theme, const Choices &choices);
    BoundGauge(const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &session,
               const Units &units, const GaugeTheme &theme)
        : BoundGauge(gauge, channel, session, units, theme, Choices())
    {
    }

    bool isEmpty() const { return m_nodes.isEmpty() || m_size.isEmpty(); }
    QSizeF size() const { return m_size; }

    // Draws the gauge into `target` (painter coordinates) with the data at
    // `time` seconds from the session start.
    void render(QPainter &painter, const QRectF &target, double time) const;

    // The scale `channel` got ("" = the bound channel). For tests and tooltips.
    MetricRange rangeOf(const QString &channel = QString()) const;

    // Where a value lies on a manipulation's scale, 0..1.
    static double fraction(const GaugeManipulation &manipulation, const MetricRange &range, double value);
    // Seconds as minutes:seconds with `decimals` of a second; `plus` before a value not below zero.
    static QString durationText(double seconds, int decimals, const QString &plus = QString());
    // Degrees as degrees, arc minutes and seconds with `decimals` of a second.
    static QString degreesText(double degrees, int decimals);

private:
    struct Metric
    {
        const Channel *channel{nullptr};  // null: not in the recording
        QString clock;                    // not a measurement but the time on the clock: its format
        ChannelDisplay display;
        MetricRange range;
        int markingCount{0};
        QStringList markings;
    };
    // One route of a map, already placed in the component's frame.
    struct Route
    {
        QPainterPath path;
        QTransform toFrame;               // projected position -> design units
        double startTime{0.0};
        double endTime{0.0};
        QPointF startPoint;
        double startAngle{0.0};           // direction of travel there, clockwise from up
        QImage map;                       // street map under it (null: none), drawn into mapRect
        QRectF mapRect;                   // design units
        // A map that follows the current position (component.span): the route as points,
        // in runs with their bounds, so that only what is near is drawn
        bool follows{false};
        QVector<QPointF> points;
        QVector<QRectF> runBounds;
    };
    struct Node
    {
        GaugeComponent component;         // without sub-components; image already colored
        QColor color;                     // text and line color
        // Shape: its outline, and the fill's gradient end (invalid = none)
        QPainterPath shape;
        QColor gradient;
        // Dial, Tape
        QColor minorColor;
        QColor majorColor;
        QColor arcColor;
        QColor markerColor;
        int metric{0};
        int marking{-1};                  // for IntervalMarking texts: which number
        QVector<Node> children;
        QVector<Node> knob;
        QVector<Node> knobTurning;        // of a map turned to the way ahead: the parts that turn with it
        QVector<Node> xMarker;
        QVector<Node> yMarker;
        QVector<Node> startLine;
        // per manipulation: the metrics a Translate2D moves by (x, y); -1 = none
        QVector<QPair<int, int>> axes;
        // AnimationStrip: running phase (pictures shown so far) at the channel's sample times
        QVector<double> phaseTimes;
        QVector<double> phases;
        // Graph
        QPainterPath line;
        QPainterPath area;
        QColor fill;
        double graphT0{0.0};              // the across axis: seconds, or meters along the distance
        double graphT1{1.0};
        double graphV0{0.0};
        double graphV1{1.0};
        // TrackMap, LapMap
        QVector<Route> routes;
        std::shared_ptr<Flyover> flyover; // in 3D (component.flyover): the land it draws, kept
        int lineSlot{-1};                 // which of the gauge's graphs and maps, for the picture cache
    };
    // How the current frame is being drawn.
    struct Context
    {
        QSize box;                        // the gauge's size on the painter, whole pixels
        qreal devicePixelRatio{1.0};
        bool cached{false};               // pictures may be used (no turned or scaled painter)
        bool topLevel{false};             // drawing one of m_nodes itself, not a part inside one
    };

    // The channel a part shows: its own, or the bound one ("" = the bound one), as chosen.
    QString shownChannel(const QString &channel) const;
    int metricFor(const QString &channel, const Units &units, const QString &unit = QString());
    // A dial's own numbers ending at m_scaleEnd.
    void rescale(GaugeScale &scale) const;
    Node bind(const GaugeComponent &component, const Units &units, const GaugeTheme &theme);
    QVector<Node> bindAll(const QVector<GaugeComponent> &components, const Units &units, const GaugeTheme &theme);
    void prepareGraph(Node &node) const;
    // A graph's across axis: the time, or the distance ridden by then (GaugeComponent::byDistance).
    bool alongDistance(const GaugeComponent &component) const;
    double axisAt(const GaugeComponent &component, double time) const;
    // ... and back: the time at that point of the axis.
    double timeOnAxis(const GaugeComponent &component, double axis) const;
    void prepareRoutes(Node &node) const;
    void drawTiles(QPainter &painter, const Node &node, int route) const;
    void drawCredit(QPainter &painter, const Node &node) const;
    void drawFollowing(QPainter &painter, const Node &node, const Route &route, double time, const Context &context) const;
    void preparePhases(Node &node, const GaugeManipulation &strip) const;
    double phaseAt(const Node &node, const GaugeManipulation &strip, double time) const;
    bool positionAt(double time, QPointF *projected) const;
    // The way the rider goes at `time`, degrees from north: over enough seconds around it that
    // the GPS's wandering and stops do not turn it.
    double headingAt(double time) const;
    void prepareFlyover(Node &node) const;
    void drawFlyover(QPainter &painter, const Node &node, double time, const Context &context) const;
    // A close-up's meters across at `time`: its span, or with SpeedZoom what the speed then calls for.
    double spanAt(const GaugeComponent &component, double time) const;
    void draw(QPainter &painter, const Node &node, double time, const Context &context) const;
    void drawAt(QPainter &painter, const QVector<Node> &nodes, const QPointF &origin, double time,
                const Context &context, double angle = 0.0) const;
    void drawGraph(QPainter &painter, const Node &node, bool known, double value, double time, const Context &context) const;
    void drawRoute(QPainter &painter, const Node &node, double time, const Context &context) const;
    // A graph's line and fill, or one route of a map, drawn once at this size.
    QImage linePicture(const Node &node, int route, const Context &context) const;
    void drawLine(QPainter &painter, const Node &node, int route) const;
    void drawText(QPainter &painter, const Node &node, const Metric &metric, bool known, double value, double time) const;
    QString clockText(const GaugeComponent &component, double time) const;
    void drawShape(QPainter &painter, const Node &node) const;
    void drawGears(QPainter &painter, const Node &node, double time) const;
    void prepareLaps();
    void drawLapTable(QPainter &painter, const Node &node, double time) const;
    void drawSectorBar(QPainter &painter, const Node &node, double time) const;
    void drawSvg(QPainter &painter, const Node &node) const;
    void drawDial(QPainter &painter, const Node &node) const;
    void drawTape(QPainter &painter, const Node &node, bool known, double value) const;
    static bool isStatic(const Node &node);
    // Pictures of the static runs of m_nodes at this size in device pixels.
    QVector<QImage> staticLayers(const QSize &size, qreal devicePixelRatio) const;

    // m_nodes split into runs that either never change or have to be drawn every frame
    struct Run
    {
        bool still{false};
        int first{0};
        int end{0};    // one past the last
    };
    QVector<Run> m_runs;
    int m_lineSlots{0};
    // Shared by copies of this gauge (the preview's and an export's), filled on first use.
    struct LayerCache
    {
        QMutex mutex;
        struct Entry
        {
            QSize size;
            qreal devicePixelRatio{1.0};
            QVector<QImage> layers;
        };
        QVector<Entry> entries;   // newest last; a few sizes at most
        // graph and route lines: slot, lap, size in device pixels -> picture
        QHash<QString, QImage> lines;
    };
    std::shared_ptr<LayerCache> m_cache;

    QSizeF m_size;
    QString m_channel;
    Choices m_choices;
    double m_scaleEnd{0.0};   // where the bound channel's dial ends: chosen, or from the data; 0 = as made
    // projection of positions onto a plane, the same for every map of the gauge
    const Channel *m_lat{nullptr};
    const Channel *m_lon{nullptr};
    const Channel *m_heading{nullptr};
    const Channel *m_speed{nullptr};
    const Channel *m_distance{nullptr};     // for graphs along the distance
    TelemetrySessionPtr m_session;        // keeps the channels alive
    GaugeTheme m_theme;                   // its font, for texts
    QVector<Metric> m_metrics;
    QHash<QString, int> m_metricIndex;
    // The laps as lap tables and sector bars show them, in order: the finished ones and the
    // one under way at the end
    struct LapSpan
    {
        int number{0};
        double start{0.0};
        double end{0.0};
        bool finished{false};
        QVector<double> splits;           // where it crossed the sector lines; empty: no sectors
    };
    QVector<LapSpan> m_laps;
    int m_sectors{0};                     // a lap's sectors; 0 = none
    QVector<Node> m_nodes;
};
