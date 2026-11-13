#pragma once

#include "../telemetry/telemetrysession.h"
#include "../units/units.h"
#include "gauge.h"
#include "gaugetheme.h"
#include "metricrange.h"

#include <QHash>
#include <QMutex>
#include <QPainterPath>

class QPainter;

// A gauge bound to a channel of a recording and ready to be drawn.
//
// Binding does everything that does not change from frame to frame: images
// are coloured for the theme, every channel the gauge uses gets its unit,
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
    BoundGauge() = default;
    // session may be null: every channel is then unknown and scales are 0..100.
    BoundGauge(const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &session,
               const Units &units, const GaugeTheme &theme);

    bool isEmpty() const { return m_nodes.isEmpty() || m_size.isEmpty(); }
    QSizeF size() const { return m_size; }

    // Draws the gauge into `target` (painter coordinates) with the data at
    // `time` seconds from the session start.
    void render(QPainter &painter, const QRectF &target, double time) const;

    // The scale `channel` got ("" = the bound channel). For tests and tooltips.
    MetricRange rangeOf(const QString &channel = QString()) const;

    // Where a value lies on a manipulation's scale, 0..1.
    static double fraction(const GaugeManipulation &manipulation, const MetricRange &range, double value);

private:
    struct Metric
    {
        const Channel *channel{nullptr};  // null: not in the recording
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
    };
    struct Node
    {
        GaugeComponent component;         // without sub-components; image already coloured
        QColor color;                     // text and line colour
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
        double graphT0{0.0};
        double graphT1{1.0};
        double graphV0{0.0};
        double graphV1{1.0};
        // TrackMap, LapMap
        QVector<Route> routes;
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

    int metricFor(const QString &channel, const Units &units, const QString &unit = QString());
    Node bind(const GaugeComponent &component, const Units &units, const GaugeTheme &theme);
    QVector<Node> bindAll(const QVector<GaugeComponent> &components, const Units &units, const GaugeTheme &theme);
    void prepareGraph(Node &node) const;
    void prepareRoutes(Node &node) const;
    void preparePhases(Node &node, const GaugeManipulation &strip) const;
    double phaseAt(const Node &node, const GaugeManipulation &strip, double time) const;
    bool positionAt(double time, QPointF *projected) const;
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
    // projection of positions onto a plane, the same for every map of the gauge
    const Channel *m_lat{nullptr};
    const Channel *m_lon{nullptr};
    double m_lonScale{1.0};
    TelemetrySessionPtr m_session;        // keeps the channels alive
    QVector<Metric> m_metrics;
    QHash<QString, int> m_metricIndex;
    QVector<Node> m_nodes;
};
