#pragma once

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

// A gauge is a stack of components drawn back to front in its own "design"
// coordinate system (Gauge::size); the renderer scales that to wherever the
// gauge is placed on the video.
//
// A gauge is not tied to one measurement. It is bound to a channel when it
// is placed (speed, heart rate...), and takes its scale, unit and label from
// that channel and its colours from the theme. Components may name another
// channel of their own (a compass ring inside a speedometer).
//
// The component set is a superset of what .veg gauge files and XML gauges
// hold, so those import one to one. Angles are degrees clockwise,
// 0 = 12 o'clock.

// Where a component gets its colour from.
struct GaugeColor
{
    enum Role
    {
        Original,    // image: drawn as it is; text: the theme's foreground
        Foreground,
        Background,
        Accent,
        Accent2,
        Custom
    };
    Role role{Original};
    QColor custom;       // for Custom

    bool operator==(const GaugeColor &other) const { return role == other.role && custom == other.custom; }
};

// Changes how a component is drawn according to a value. The value's place
// between the scale's ends (0..1) drives it.
struct GaugeManipulation
{
    enum Type
    {
        Rotate,      // turn around `pivot` from minAngle to maxAngle: needles
        RadialClip,  // show the pie slice from minAngle up to the current angle: arc fills
        Translate,   // move by minX..maxX and minY..maxY: tapes, sliders
        WidthClip,   // show from x = minX up to the current x: horizontal bars
        HeightClip,  // show from y = minY up to the current y: vertical bars
        StaticRadialClip, // always show the slice minAngle..maxAngle: a mask, no value involved
        Translate2D, // move within a circle of maxRadius by two values: g-force ball
        AnimationStrip // play a strip of pictures faster the higher the value: beating heart
    };
    Type type{Rotate};
    QPointF pivot;
    double minAngle{0.0};
    double maxAngle{0.0};
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};

    // Scale ends in display units. Without them the channel's own range is used.
    bool hasValueRange{false};
    double minValue{0.0};
    double maxValue{0.0};
    bool wrapValue{false};   // past the end start over (compass) instead of stopping
    bool reversed{false};    // the scale runs from max to min
    // A scale that is not even: (value, place on the scale 0..1) in rising
    // order of value, the ends included. Between two of them it is linear.
    QVector<QPointF> curve;

    QRectF clipRect;         // null = none; in design units, not moved by the manipulation

    // WidthClip / HeightClip: the moving edges are slanted by this many
    // degrees. A horizontal bar's edge leans right at the top for positive
    // angles; a vertical bar's edge rises to the left.
    double clipAngle{0.0};

    // Translate2D: x follows xChannel, y follows yChannel (empty = taken from
    // the component's channel, see BoundGauge). Both scales are symmetric
    // around zero and reach the circle's edge at their largest value, or at
    // the larger end of the value range when there is one.
    double maxRadius{0.0};
    bool invertX{false};
    bool invertY{false};
    QString xChannel;
    QString yChannel;

    // AnimationStrip: the component's image holds `frames` pictures side by
    // side (or one above the other); frameRect is where one of them shows.
    int frames{1};
    double minFps{1.0};
    double maxFps{1.0};
    QRectF frameRect;
};

// Tick marks of a scale.
struct GaugeTicks
{
    double unit{0.0};        // value between two ticks; 0 = none
    double length{0.0};
    double width{1.0};
    GaugeColor color;

    bool operator==(const GaugeTicks &other) const
    {
        return unit == other.unit && length == other.length && width == other.width && color == other.color;
    }
};

// A scale with ticks and numbers. Dial: round, fixed, around the middle of
// the component's frame. Tape: straight, sliding so that the current value
// stays in the middle.
struct GaugeScale
{
    double minValue{0.0};
    double maxValue{100.0};
    bool loop{false};            // Tape: after the end the scale starts over (compass)
    GaugeTicks minor;
    GaugeTicks major;            // these carry the numbers
    double labelOffset{0.0};     // Dial: from the arc inwards; Tape: beyond the tick ends

    // Dial
    double startAngle{0.0};
    double endAngle{0.0};
    double radius{0.0};
    double arcWidth{0.0};        // 0 = no arc drawn
    GaugeColor arcColor;

    // Tape. Orientation: where the numbers are.
    enum Orientation { NumbersAbove, NumbersBelow, NumbersLeft, NumbersRight };
    Orientation orientation{NumbersAbove};
    bool reverse{false};         // values grow to the left / downwards
    double length{200.0};        // of the part that shows
    double spacing{25.0};        // between two major ticks
    double markerLength{0.0};    // the line at the current value; 0 = none
    double markerWidth{1.0};
    GaugeColor markerColor;
};

struct GaugeComponent
{
    enum Type
    {
        Image,
        Text,
        Group,   // children drawn together under this component's manipulations
        Graph,   // the channel over the recording as a line, with markers at the current point
        TrackMap,// the route from the position channels, with `knob` at the current position
        LapMap,  // the route of the current lap, with `startLine` where the lap began
        Shape,   // a rectangle, ellipse, arc, pie slice or line drawn with a pen or filled
        Dial,    // a round scale: arc, ticks and numbers (`scale`)
        Tape     // a sliding straight scale (`scale`)
    };
    enum ShapeKind
    {
        Rectangle,   // `frame`, corners rounded by cornerRadius
        Ellipse,     // inside `frame`
        Arc,         // of the ellipse inside `frame`, from startAngle turning sweepAngle
        Pie,         // the same, filled to the middle
        Line         // through `points`
    };
    enum ValueFormat
    {
        Number,          // with `precision` decimals
        WholeNumber,     // the decimals cut off, not rounded
        Duration,        // minutes:seconds, `precision` decimals of a second
        DegreesMinutesSeconds,
        Clock            // the time of day of the recording, `clockFormat` (a QDateTime format)
    };
    enum AutoText
    {
        FixedText,       // `text` as it is
        Value,           // "25"
        Unit,            // "km/h"
        ValueAndUnit,    // "25 km/h"
        Label,           // "Speed"
        LabelValueUnit,  // "Speed 25 km/h"
        IntervalMarking  // a number on the scale; the n-th such text gets the n-th value
    };

    Type type{Image};
    // Image: where it is drawn. Text: only the top left corner counts, as the anchor point.
    QRectF frame;
    GaugeColor color;
    // Channel this component follows; empty = the one the gauge is bound to.
    QString channel;
    // Values in this unit ("mph", "ft"...) whatever the display units are, for
    // scales printed in one. Ignored when the channel does not measure that.
    QString unit;
    QVector<GaugeManipulation> manipulations;
    bool hideWhenUnknown{false};   // not drawn where the channel has no data

    // Image
    QString imagePath;             // relative to the gauge's folder; empty for images made in code
    QImage image;
    // A drawing instead of a picture (an .svg file), drawn sharp at any size.
    // It may name theme colours as @foreground, @background, @accent and @accent2.
    QByteArray svg;
    bool channelIcon{false};       // instead of `image`, a picture of the measurement (heart, mountain...)

    // Text
    AutoText autoText{FixedText};
    QString text;
    QString fontFamily;            // empty = application font
    double fontSize{24.0};         // pixel size in design units
    bool bold{false};
    bool italic{false};
    int precision{-1};             // decimals of a value; -1 = what suits the channel
    Qt::Alignment alignment{Qt::AlignLeft | Qt::AlignVCenter}; // of the text around the anchor
    bool upperCase{false};
    // The whole frame holds the text, aligned inside it and wrapped at its width.
    bool boxed{false};
    ValueFormat valueFormat{Number};
    QString clockFormat;           // Clock
    bool localTime{true};          // Clock: the recording's local time, not UTC
    bool showPlus{false};          // "+" before positive values
    bool clampValue{false};        // values shown no lower than clampMin, no higher than clampMax
    double clampMin{0.0};
    double clampMax{0.0};

    // Group
    QVector<GaugeComponent> children;

    // Shape. The colour is the pen's or the fill's. A fill may run from
    // `color` at gradientLine's start to gradientColor at its end.
    ShapeKind shape{Rectangle};
    bool filled{true};
    double cornerRadius{0.0};
    double startAngle{0.0};
    double sweepAngle{0.0};
    QVector<QPointF> points;
    GaugeColor gradientColor;      // Original = no gradient
    QLineF gradientLine;

    // Dial, Tape. Numbers in the text font, alignment and colour above.
    GaugeScale scale;

    // Graph, TrackMap, LapMap; Shape drawn with a pen. `frame` is the area drawn into.
    double lineWidth{2.0};
    double padding{0.0};           // kept free inside the frame
    int fillAlpha{-1};             // Graph: area under the line, 0..255; -1 = not filled
    QColor fillColor;              // its colour; invalid = the line's colour
    // Graph: only this many seconds before and after the current point; 0 = the whole recording
    double windowBefore{0.0};
    double windowAfter{0.0};
    // Drawn with their origin at the current point (graph line, map position)
    QVector<GaugeComponent> knob;
    // Graph: with their origin at the current x on the top edge / at the current y on the left edge
    QVector<GaugeComponent> xMarker;
    QVector<GaugeComponent> yMarker;
    // LapMap: with their origin where the lap starts, turned across the direction of travel
    QVector<GaugeComponent> startLine;
};

struct Gauge
{
    QString name;
    QSizeF size{400.0, 400.0};     // design units
    // Channels it was designed for / is unsuitable for. Advice for the user
    // interface only; any gauge can be bound to any channel.
    QStringList supportedTypes;
    QStringList unsupportedTypes;
    QVector<GaugeComponent> components;

    bool isEmpty() const { return components.isEmpty() || size.isEmpty(); }
};
