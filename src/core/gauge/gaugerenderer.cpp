#include "gaugerenderer.h"

#include "../telemetry/channelids.h"
#include "channelicons.h"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
const double kPi = 3.14159265358979323846;
// most points a graph line or a route is drawn with
const int kMaxPathPoints = 2000;

QColor themeColor(const GaugeColor &color, const GaugeTheme &theme)
{
    switch (color.role)
        {
        case GaugeColor::Background:
            return theme.background;
        case GaugeColor::Accent:
            return theme.accent;
        case GaugeColor::Accent2:
            return theme.accent2;
        case GaugeColor::Custom:
            return color.custom;
        default:
            return theme.foreground;
        }
}

// A drawing with the theme's colours put in for @foreground, @background,
// @accent and @accent2, and their transparency for @foreground-opacity and
// so on (the SVG renderer reads no transparency in a colour itself).
QByteArray themedSvg(QByteArray svg, const GaugeTheme &theme)
{
    const QPair<const char *, QColor> colours[] = {{"foreground", theme.foreground}, {"background", theme.background},
        {"accent2", theme.accent2}, {"accent", theme.accent}
    };
    // the longer names first: "@accent" is the start of "@accent2"
    for (const auto &colour : colours)
        {
            const QByteArray name = QByteArray("@") + colour.first;
            svg.replace(name + "-opacity", QByteArray::number(colour.second.alphaF(), 'f', 3));
            svg.replace(name, colour.second.name(QColor::HexRgb).toLatin1());
        }
    return svg;
}

// Parsing a drawing for every frame would cost more than drawing it. Each
// thread keeps its own: preview and export draw at the same time.
QSvgRenderer *svgRenderer(const QByteArray &svg)
{
    thread_local QHash<QByteArray, std::shared_ptr<QSvgRenderer>> cache;
    auto found = cache.find(svg);
    if (found == cache.end())
        {
            if (cache.size() > 200)
                {
                    cache.clear();
                }
            found = cache.insert(svg, std::make_shared<QSvgRenderer>(svg));
        }
    return found.value().get();
}

// Gauge artwork is a shape, not a picture: only its transparency is kept
// and the colour comes from the theme.
QImage tinted(const QImage &image, const QColor &color)
{
    if (image.isNull())
        {
            return image;
        }
    QImage result = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

// Most gauge artwork is a full-size picture with a little in it: a needle is
// a 400 x 400 image holding a thin bar. Drawing (and turning) it costs every
// pixel, so the empty border is cut off and the frame shrunk to match.
void cropToContent(QImage *image, QRectF *frame)
{
    if (image->isNull() || frame->isEmpty())
        {
            return;
        }
    const QImage source = image->convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int left = source.width();
    int right = -1;
    int top = source.height();
    int bottom = -1;
    for (int y = 0; y < source.height(); ++y)
        {
            const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            for (int x = 0; x < source.width(); ++x)
                {
                    if (qAlpha(line[x]) != 0)
                        {
                            left = std::min(left, x);
                            right = std::max(right, x);
                            top = std::min(top, y);
                            bottom = std::max(bottom, y);
                        }
                }
        }
    if (right < 0)
        {
            *image = QImage();   // nothing to draw at all
            return;
        }
    // one pixel of margin, so smooth scaling still has its edge to blend with
    left = std::max(0, left - 1);
    top = std::max(0, top - 1);
    right = std::min(source.width() - 1, right + 1);
    bottom = std::min(source.height() - 1, bottom + 1);
    if (left == 0 && top == 0 && right == source.width() - 1 && bottom == source.height() - 1)
        {
            *image = source;
            return;
        }
    const double sx = frame->width() / source.width();
    const double sy = frame->height() / source.height();
    *frame = QRectF(frame->x() + left * sx, frame->y() + top * sy, (right - left + 1) * sx, (bottom - top + 1) * sy);
    *image = source.copy(left, top, right - left + 1, bottom - top + 1);
}

double lerp(double from, double to, double f)
{
    return from + (to - from) * f;
}

// Douglas-Peucker: the fewest points that stay within `tolerance` of the
// line through all of them. Routes and graphs come with far more points than
// the pixels they are drawn on, and every point costs when the line is drawn.
QVector<QPointF> simplified(const QVector<QPointF> &points, double tolerance)
{
    if (points.size() < 3)
        {
            return points;
        }
    QVector<bool> keep(points.size(), false);
    keep.first() = true;
    keep.last() = true;
    QVector<QPair<int, int>> pending = {{0, int(points.size()) - 1}};
    while (!pending.isEmpty())
        {
            const QPair<int, int> span = pending.takeLast();
            const QPointF a = points.at(span.first);
            const QPointF b = points.at(span.second);
            const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
            double farthest = -1.0;
            int index = -1;
            for (int i = span.first + 1; i < span.second; ++i)
                {
                    const QPointF p = points.at(i);
                    const double distance = length > 0.0
                                            ? std::fabs((b.x() - a.x()) * (a.y() - p.y()) - (a.x() - p.x()) * (b.y() - a.y())) / length
                                            : std::hypot(p.x() - a.x(), p.y() - a.y());
                    if (distance > farthest)
                        {
                            farthest = distance;
                            index = i;
                        }
                }
            if (index >= 0 && farthest > tolerance)
                {
                    keep[index] = true;
                    pending.append({span.first, index});
                    pending.append({index, span.second});
                }
        }
    QVector<QPointF> result;
    for (int i = 0; i < points.size(); ++i)
        {
            if (keep.at(i))
                {
                    result.append(points.at(i));
                }
        }
    return result;
}

QPainterPath polyline(const QVector<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty())
        {
            return path;
        }
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i)
        {
            path.lineTo(points.at(i));
        }
    return path;
}

// a quarter of a design unit: never visible, whatever size the gauge is drawn at
const double kLineTolerance = 0.25;

// The two channels a gauge made for a two-value type moves by.
QPair<QString, QString> axesOf(const QString &channel)
{
    if (channel == QLatin1String("gForceLatLon"))
        {
            return {ChannelId::GForceLateral, ChannelId::GForceLongitudinal};
        }
    if (channel == QLatin1String("gForceLatVert"))
        {
            return {ChannelId::GForceLateral, ChannelId::GForceVertical};
        }
    return {channel, QString()};
}

// Clip shapes only have to reach past what can be seen. Anything larger
// costs the rasteriser for every pixel of length, and these are made anew
// for every frame. `reach` is a distance beyond which nothing of the gauge lies.

// The slice from `fromAngle` turning `sweep` degrees clockwise (0 = 12 o'clock),
// as a polygon whose outer edge stays beyond `reach` from the pivot.
QPainterPath slice(const QPointF &pivot, double fromAngle, double sweep, double reach)
{
    const int steps = std::max(1, int(std::ceil(std::fabs(sweep) / 10.0)));
    // the chords of the outer edge must not cut inside `reach`
    const double radius = reach / std::cos(sweep / steps / 2.0 * kPi / 180.0) + 1.0;
    QPolygonF polygon;
    polygon << pivot;
    for (int i = 0; i <= steps; ++i)
        {
            const double angle = (fromAngle + sweep * i / steps) * kPi / 180.0;
            polygon << QPointF(pivot.x() + radius * std::sin(angle), pivot.y() - radius * std::cos(angle));
        }
    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    return path;
}

// The band between two vertical lines at x0 and x1, both slanted by `angle`
// around the height yc (top leans right for positive angles), from -reach to reach.
QPainterPath verticalBand(double x0, double x1, double yc, double angle, double reach)
{
    const double k = std::tan(angle * kPi / 180.0);
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    QPainterPath path;
    path.moveTo(left + (yc + reach) * k, -reach);
    path.lineTo(right + (yc + reach) * k, -reach);
    path.lineTo(right + (yc - reach) * k, reach);
    path.lineTo(left + (yc - reach) * k, reach);
    path.closeSubpath();
    return path;
}

// The band between two horizontal lines at y0 and y1, both slanted by
// `angle` around x = xc (rising to the left for positive angles).
QPainterPath horizontalBand(double y0, double y1, double xc, double angle, double reach)
{
    const double k = std::tan(angle * kPi / 180.0);
    const double top = std::min(y0, y1);
    const double bottom = std::max(y0, y1);
    QPainterPath path;
    path.moveTo(-reach, top + (-reach - xc) * k);
    path.lineTo(reach, top + (reach - xc) * k);
    path.lineTo(reach, bottom + (reach - xc) * k);
    path.lineTo(-reach, bottom + (-reach - xc) * k);
    path.closeSubpath();
    return path;
}

// QPainterPath counts angles anticlockwise from 3 o'clock.
double qtAngle(double angle)
{
    return 90.0 - angle;
}

QPainterPath shapePath(const GaugeComponent &c)
{
    QPainterPath path;
    switch (c.shape)
        {
        case GaugeComponent::Rectangle:
            if (c.cornerRadius > 0.0)
                {
                    const double radius = std::min({c.cornerRadius, c.frame.width() / 2.0, c.frame.height() / 2.0});
                    path.addRoundedRect(c.frame, radius, radius);
                }
            else
                {
                    path.addRect(c.frame);
                }
            break;
        case GaugeComponent::Ellipse:
            path.addEllipse(c.frame);
            break;
        case GaugeComponent::Arc:
            path.arcMoveTo(c.frame, qtAngle(c.startAngle));
            path.arcTo(c.frame, qtAngle(c.startAngle), -c.sweepAngle);
            break;
        case GaugeComponent::Pie:
            path.moveTo(c.frame.center());
            path.arcTo(c.frame, qtAngle(c.startAngle), -c.sweepAngle);
            path.closeSubpath();
            break;
        case GaugeComponent::Line:
            if (!c.points.isEmpty())
                {
                    path.moveTo(c.points.first());
                    for (int i = 1; i < c.points.size(); ++i)
                        {
                            path.lineTo(c.points.at(i));
                        }
                }
            break;
        }
    return path;
}

QFont fontOf(const QPainter &painter, const GaugeComponent &c)
{
    QFont font = painter.font();
    if (!c.fontFamily.isEmpty())
        {
            font.setFamily(c.fontFamily);
        }
    font.setPixelSize(std::max(1, int(std::lround(c.fontSize))));
    font.setBold(c.bold);
    font.setItalic(c.italic);
    return font;
}

// `alignment` says which point of the text sits on `anchor`.
void drawAnchored(QPainter &painter, const QString &text, const QPointF &anchor, Qt::Alignment alignment)
{
    const QFontMetricsF metrics(painter.font());
    double x = anchor.x();
    if (alignment & Qt::AlignHCenter)
        {
            x -= metrics.horizontalAdvance(text) / 2.0;
        }
    else if (alignment & Qt::AlignRight)
        {
            x -= metrics.horizontalAdvance(text);
        }
    double baseline = anchor.y() + (metrics.ascent() - metrics.descent()) / 2.0; // centred
    if (alignment & Qt::AlignTop)
        {
            baseline = anchor.y() + metrics.ascent();
        }
    else if (alignment & Qt::AlignBottom)
        {
            baseline = anchor.y() - metrics.descent();
        }
    painter.drawText(QPointF(x, baseline), text);
}

// A number on a scale: "0.5", "10", "-2.25"; sums of steps lose their last digits.
QString scaleNumber(double value)
{
    return QString::number(std::fabs(value) < 1e-9 ? 0.0 : value, 'g', 12);
}

// a pen of no width is a hairline in Qt, but means "not drawn" in a gauge
bool setPen(QPainter &painter, const QColor &color, double width)
{
    if (width <= 0.0 || color.alpha() == 0)
        {
            return false;
        }
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    return true;
}
}

BoundGauge::BoundGauge(const Gauge &gauge, const QString &channel, const TelemetrySessionPtr &session,
                       const Units &units, const GaugeTheme &theme)
    : m_size(gauge.size)
    , m_channel(channel)
    , m_session(session)
{
    if (m_session)
        {
            m_lat = m_session->channel(ChannelId::PositionLat);
            m_lon = m_session->channel(ChannelId::PositionLon);
            if (m_lat && m_lon && !m_lat->isEmpty())
                {
                    // equirectangular around the middle of the route: good enough for a ride
                    const double middle = (m_lat->minValue() + m_lat->maxValue()) / 2.0;
                    m_lonScale = std::cos(middle * kPi / 180.0);
                }
            else
                {
                    m_lat = m_lon = nullptr;
                }
        }
    metricFor(QString(), units); // index 0: the bound channel
    m_nodes = bindAll(gauge.components, units, theme);
    // scales can be set only now that every marking has been counted
    for (Metric &metric : m_metrics)
        {
            const bool hasData = metric.channel && !metric.channel->isEmpty();
            metric.range = hasData
                           ? MetricRange::covering(metric.display.convert(metric.channel->minValue()),
                                                   metric.display.convert(metric.channel->maxValue()), metric.markingCount)
                           : MetricRange::covering(0.0, 100.0, metric.markingCount);
            metric.markings = metric.range.markingTexts(metric.markingCount);
        }
    // these need the scales
    std::function<void(QVector<Node> &)> finish = [&](QVector<Node> &nodes)
    {
        for (Node &node : nodes)
            {
                for (const GaugeManipulation &m : node.component.manipulations)
                    {
                        if (m.type == GaugeManipulation::AnimationStrip)
                            {
                                preparePhases(node, m);
                            }
                    }
                if (node.component.type == GaugeComponent::Graph)
                    {
                        prepareGraph(node);
                        node.lineSlot = m_lineSlots++;
                    }
                else if (node.component.type == GaugeComponent::TrackMap || node.component.type == GaugeComponent::LapMap)
                    {
                        prepareRoutes(node);
                        node.lineSlot = m_lineSlots++;
                    }
                for (QVector<Node> *list : {&node.children, &node.knob, &node.xMarker, &node.yMarker, &node.startLine})
                    {
                        finish(*list);
                    }
            }
    };
    finish(m_nodes);

    for (int i = 0; i < m_nodes.size(); ++i)
        {
            const bool still = isStatic(m_nodes.at(i));
            if (m_runs.isEmpty() || m_runs.last().still != still)
                {
                    m_runs.append({still, i, i});
                }
            m_runs.last().end = i + 1;
        }
    m_cache = std::make_shared<LayerCache>();
}

bool BoundGauge::isStatic(const Node &node)
{
    const GaugeComponent &c = node.component;
    if (!c.manipulations.isEmpty() || c.hideWhenUnknown)
        {
            return false;
        }
    switch (c.type)
        {
        case GaugeComponent::Image:
        case GaugeComponent::Shape:
        case GaugeComponent::Dial:
            return true;
        case GaugeComponent::Text:
            // settled once the gauge is bound; values change
            return c.autoText == GaugeComponent::FixedText || c.autoText == GaugeComponent::Unit
                   || c.autoText == GaugeComponent::Label || c.autoText == GaugeComponent::IntervalMarking;
        case GaugeComponent::Group:
            for (const Node &child : node.children)
                {
                    if (!isStatic(child))
                        {
                            return false;
                        }
                }
            return true;
        default:
            return false;   // graphs and maps carry markers that move
        }
}

QVector<QImage> BoundGauge::staticLayers(const QSize &size, qreal devicePixelRatio) const
{
    const QMutexLocker lock(&m_cache->mutex);
    for (const LayerCache::Entry &entry : m_cache->entries)
        {
            if (entry.size == size && entry.devicePixelRatio == devicePixelRatio)
                {
                    return entry.layers;
                }
        }
    LayerCache::Entry entry;
    entry.size = size;
    entry.devicePixelRatio = devicePixelRatio;
    for (const Run &run : m_runs)
        {
            if (!run.still)
                {
                    continue;
                }
            QImage layer(size * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
            layer.setDevicePixelRatio(devicePixelRatio);
            layer.fill(Qt::transparent);
            QPainter painter(&layer);
            painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
            painter.scale(size.width() / m_size.width(), size.height() / m_size.height());
            painter.setClipRect(QRectF(QPointF(0, 0), m_size));
            for (int i = run.first; i < run.end; ++i)
                {
                    draw(painter, m_nodes.at(i), 0.0, Context());
                }
            painter.end();
            entry.layers.append(layer);
        }
    // the preview, an export and a thumbnail at most: older sizes are forgotten
    m_cache->entries.append(entry);
    if (m_cache->entries.size() > 4)
        {
            m_cache->entries.removeFirst();
        }
    return entry.layers;
}

int BoundGauge::metricFor(const QString &channel, const Units &units, const QString &unit)
{
    const QString id = channel.isEmpty() ? m_channel : channel;
    // the same channel in a unit of its own is a scale of its own
    const QString key = unit.isEmpty() ? id : id + QLatin1Char('\n') + unit;
    const auto known = m_metricIndex.constFind(key);
    if (known != m_metricIndex.constEnd())
        {
            return known.value();
        }
    Metric metric;
    metric.channel = m_session ? m_session->channel(id) : nullptr;
    const QString storedUnit = metric.channel ? metric.channel->unit() : QString();
    metric.display = unit.isEmpty() ? ChannelFormat::displayFor(id, storedUnit, units)
                     : ChannelFormat::displayIn(id, storedUnit, unit, units);
    m_metrics.append(metric);
    m_metricIndex.insert(key, m_metrics.size() - 1);
    return m_metrics.size() - 1;
}

QVector<BoundGauge::Node> BoundGauge::bindAll(const QVector<GaugeComponent> &components, const Units &units,
        const GaugeTheme &theme)
{
    QVector<Node> nodes;
    for (const GaugeComponent &component : components)
        {
            nodes.append(bind(component, units, theme));
        }
    return nodes;
}

BoundGauge::Node BoundGauge::bind(const GaugeComponent &component, const Units &units, const GaugeTheme &theme)
{
    Node node;
    node.component = component;
    node.component.children.clear();
    node.component.knob.clear();
    node.component.xMarker.clear();
    node.component.yMarker.clear();
    node.component.startLine.clear();
    node.metric = metricFor(component.channel, units, component.unit);
    node.color = themeColor(component.color, theme);
    const QString channelId = component.channel.isEmpty() ? m_channel : component.channel;
    if (component.type == GaugeComponent::Shape)
        {
            node.shape = shapePath(component);
            if (component.gradientColor.role != GaugeColor::Original)
                {
                    node.gradient = themeColor(component.gradientColor, theme);
                }
        }
    if (component.type == GaugeComponent::Dial || component.type == GaugeComponent::Tape)
        {
            node.minorColor = themeColor(component.scale.minor.color, theme);
            node.majorColor = themeColor(component.scale.major.color, theme);
            node.arcColor = themeColor(component.scale.arcColor, theme);
            node.markerColor = themeColor(component.scale.markerColor, theme);
        }
    if (component.type == GaugeComponent::Image && !component.svg.isEmpty())
        {
            node.component.svg = themedSvg(component.svg, theme);
        }
    else if (component.type == GaugeComponent::Image)
        {
            if (component.channelIcon)
                {
                    node.component.image = ChannelIcons::icon(channelId);
                }
            if (component.color.role != GaugeColor::Original || component.channelIcon)
                {
                    node.component.image = tinted(node.component.image, node.color);
                }
            // An animation strip's pictures sit at fixed places in the image;
            // cutting the margins keeps them there, as the frame shrinks along.
            cropToContent(&node.component.image, &node.component.frame);
        }
    if (component.type == GaugeComponent::Text && component.autoText == GaugeComponent::IntervalMarking)
        {
            node.marking = m_metrics[node.metric].markingCount++;
        }
    for (const GaugeManipulation &m : component.manipulations)
        {
            QPair<int, int> axes(-1, -1);
            if (m.type == GaugeManipulation::Translate2D)
                {
                    const QPair<QString, QString> derived = axesOf(channelId);
                    const QString x = m.xChannel.isEmpty() ? derived.first : m.xChannel;
                    const QString y = m.yChannel.isEmpty() ? derived.second : m.yChannel;
                    axes = qMakePair(x.isEmpty() ? -1 : metricFor(x, units, component.unit),
                                     y.isEmpty() ? -1 : metricFor(y, units, component.unit));
                }
            node.axes.append(axes);
        }
    if (component.fillAlpha >= 0)
        {
            node.fill = component.fillColor.isValid() ? component.fillColor : node.color;
            node.fill.setAlpha(component.fillAlpha);
        }
    node.children = bindAll(component.children, units, theme);
    node.knob = bindAll(component.knob, units, theme);
    node.xMarker = bindAll(component.xMarker, units, theme);
    node.yMarker = bindAll(component.yMarker, units, theme);
    node.startLine = bindAll(component.startLine, units, theme);
    return node;
}

// --- preparation, once per binding ---------------------------------------

void BoundGauge::prepareGraph(Node &node) const
{
    const Metric &metric = m_metrics.at(node.metric);
    if (!metric.channel || metric.channel->count() < 2)
        {
            return;
        }
    const QRectF frame = node.component.frame;
    node.graphT0 = metric.channel->startTime();
    node.graphT1 = metric.channel->endTime();
    const int points = std::clamp(int(frame.width()), 50, kMaxPathPoints);
    QVector<double> values(points);
    double low = INFINITY;
    double high = -INFINITY;
    for (int i = 0; i < points; ++i)
        {
            const double t = lerp(node.graphT0, node.graphT1, double(i) / (points - 1));
            values[i] = metric.display.convert(metric.channel->valueAt(t));
            low = std::min(low, values[i]);
            high = std::max(high, values[i]);
        }
    if (high - low < 1e-9)
        {
            // a flat line sits in the middle
            low -= 1.0;
            high += 1.0;
        }
    node.graphV0 = low;
    node.graphV1 = high;
    QVector<QPointF> line(points);
    for (int i = 0; i < points; ++i)
        {
            line[i] = QPointF(lerp(frame.left(), frame.right(), double(i) / (points - 1)),
                              frame.bottom() - (values[i] - low) / (high - low) * frame.height());
        }
    node.line = polyline(simplified(line, kLineTolerance));
    node.area = node.line;
    node.area.lineTo(frame.right(), frame.bottom());
    node.area.lineTo(frame.left(), frame.bottom());
    node.area.closeSubpath();
}

bool BoundGauge::positionAt(double time, QPointF *projected) const
{
    if (!m_lat || !m_lon)
        {
            return false;
        }
    bool latOk = false;
    bool lonOk = false;
    const double lat = m_lat->valueAt(time, &latOk);
    const double lon = m_lon->valueAt(time, &lonOk);
    // north up: y grows downwards on screen
    *projected = QPointF(lon * m_lonScale, -lat);
    return latOk && lonOk;
}

void BoundGauge::prepareRoutes(Node &node) const
{
    if (!m_lat || !m_lon || m_lat->count() < 2)
        {
            return;
        }
    // what to draw: the whole recording, or one route per lap
    QVector<QPair<double, double>> spans;
    if (node.component.type == GaugeComponent::LapMap)
        {
            for (const TelemetrySession::Lap &lap : m_session->laps())
                {
                    spans.append({lap.startTime, lap.endTime});
                }
        }
    if (spans.isEmpty())
        {
            spans.append({m_lat->startTime(), m_lat->endTime()});
        }

    const QRectF area = node.component.frame.adjusted(node.component.padding, node.component.padding,
                        -node.component.padding, -node.component.padding);
    for (const auto &span : spans)
        {
            // evenly in time across the span, as many points as a path needs and no more
            const int count = std::clamp(m_lat->count(), 2, kMaxPathPoints);
            QVector<QPointF> points;
            for (int i = 0; i < count; ++i)
                {
                    QPointF p;
                    if (positionAt(lerp(span.first, span.second, double(i) / (count - 1)), &p))
                        {
                            points.append(p);
                        }
                }
            if (points.size() < 2)
                {
                    continue;
                }
            // by hand: QRectF::united() skips rectangles of no size, and points have none
            double left = points.first().x();
            double right = left;
            double top = points.first().y();
            double bottom = top;
            for (const QPointF &p : points)
                {
                    left = std::min(left, p.x());
                    right = std::max(right, p.x());
                    top = std::min(top, p.y());
                    bottom = std::max(bottom, p.y());
                }
            const QRectF bounds(QPointF(left, top), QPointF(right, bottom));
            // keep the shape: one scale for both directions, centred
            const double scale = std::min(area.width() / std::max(bounds.width(), 1e-9),
                                          area.height() / std::max(bounds.height(), 1e-9));
            Route route;
            route.toFrame.translate(area.center().x(), area.center().y());
            route.toFrame.scale(scale, scale);
            route.toFrame.translate(-bounds.center().x(), -bounds.center().y());
            QVector<QPointF> placed;
            placed.reserve(points.size());
            for (const QPointF &p : points)
                {
                    placed.append(route.toFrame.map(p));
                }
            route.path = polyline(simplified(placed, kLineTolerance));
            route.startTime = span.first;
            route.endTime = span.second;
            route.startPoint = route.toFrame.map(points.first());
            // direction: towards the first point a little way along
            for (int i = 1; i < points.size(); ++i)
                {
                    const QPointF delta = route.toFrame.map(points.at(i)) - route.startPoint;
                    if (std::hypot(delta.x(), delta.y()) > 3.0)
                        {
                            route.startAngle = std::atan2(delta.x(), -delta.y()) * 180.0 / kPi;
                            break;
                        }
                }
            node.routes.append(route);
        }
}

void BoundGauge::preparePhases(Node &node, const GaugeManipulation &strip) const
{
    const Metric &metric = m_metrics.at(node.metric);
    if (!metric.channel || metric.channel->count() < 2)
        {
            return;
        }
    // pictures shown so far = the integral of the frame rate over time, one step per second or so
    const double start = metric.channel->startTime();
    const double end = metric.channel->endTime();
    const int steps = std::clamp(int(end - start), 2, 50000);
    double phase = 0.0;
    double previous = start;
    for (int i = 0; i < steps; ++i)
        {
            const double t = lerp(start, end, double(i) / (steps - 1));
            const double value = metric.display.convert(metric.channel->valueAt(t));
            const double fps = lerp(strip.minFps, strip.maxFps, fraction(GaugeManipulation(), metric.range, value));
            phase += fps * (t - previous);
            node.phaseTimes.append(t);
            node.phases.append(phase);
            previous = t;
        }
}

double BoundGauge::phaseAt(const Node &node, const GaugeManipulation &strip, double time) const
{
    if (node.phaseTimes.isEmpty())
        {
            return time * strip.minFps;
        }
    if (time <= node.phaseTimes.first())
        {
            return node.phases.first() - (node.phaseTimes.first() - time) * strip.minFps;
        }
    if (time >= node.phaseTimes.last())
        {
            return node.phases.last() + (time - node.phaseTimes.last()) * strip.minFps;
        }
    const int next = int(std::upper_bound(node.phaseTimes.cbegin(), node.phaseTimes.cend(), time) - node.phaseTimes.cbegin());
    const double f = (time - node.phaseTimes.at(next - 1)) / (node.phaseTimes.at(next) - node.phaseTimes.at(next - 1));
    return lerp(node.phases.at(next - 1), node.phases.at(next), f);
}

// --- drawing -------------------------------------------------------------

MetricRange BoundGauge::rangeOf(const QString &channel) const
{
    const int index = m_metricIndex.value(channel.isEmpty() ? m_channel : channel, -1);
    return index >= 0 ? m_metrics.at(index).range : MetricRange();
}

double BoundGauge::fraction(const GaugeManipulation &manipulation, const MetricRange &range, double value)
{
    const QVector<QPointF> &curve = manipulation.curve;
    const bool curved = curve.size() >= 2 && curve.last().x() > curve.first().x();
    const double low = curved ? curve.first().x() : manipulation.hasValueRange ? manipulation.minValue : range.min;
    const double high = curved ? curve.last().x() : manipulation.hasValueRange ? manipulation.maxValue : range.max;
    double f = 0.0;
    if (high != low && std::isfinite(value))
        {
            f = (value - low) / (high - low);
            f = manipulation.wrapValue ? f - std::floor(f) : std::clamp(f, 0.0, 1.0);
        }
    if (curved)
        {
            const double v = lerp(low, high, f);
            int next = 1;
            while (next < curve.size() - 1 && curve.at(next).x() < v)
                {
                    ++next;
                }
            const QPointF a = curve.at(next - 1);
            const QPointF b = curve.at(next);
            f = b.x() > a.x() ? lerp(a.y(), b.y(), (v - a.x()) / (b.x() - a.x())) : b.y();
        }
    return manipulation.reversed ? 1.0 - f : f;
}

void BoundGauge::render(QPainter &painter, const QRectF &target, double time) const
{
    // on whole pixels, so the cached and the live parts meet exactly
    const QRect box(int(std::lround(target.x())), int(std::lround(target.y())),
                    int(std::lround(target.width())), int(std::lround(target.height())));
    if (isEmpty() || box.isEmpty())
        {
            return;
        }
    // cached pictures only fit a painter that does not scale or turn them
    Context context;
    context.box = box.size();
    context.cached = painter.worldTransform().type() <= QTransform::TxTranslate;
    context.devicePixelRatio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    context.topLevel = true;
    const QVector<QImage> layers = context.cached ? staticLayers(box.size(), context.devicePixelRatio) : QVector<QImage>();

    painter.save();
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    int layer = 0;
    for (const Run &run : m_runs)
        {
            if (run.still && context.cached)
                {
                    painter.drawImage(box.topLeft(), layers.at(layer++));
                    continue;
                }
            painter.save();
            painter.translate(box.topLeft());
            painter.scale(box.width() / m_size.width(), box.height() / m_size.height());
            // a gauge is a window: tapes and rings are far bigger than what shows of them
            painter.setClipRect(QRectF(QPointF(0, 0), m_size), Qt::IntersectClip);
            for (int i = run.first; i < run.end; ++i)
                {
                    draw(painter, m_nodes.at(i), time, context);
                }
            painter.restore();
        }
    painter.restore();
}

void BoundGauge::drawAt(QPainter &painter, const QVector<Node> &nodes, const QPointF &origin, double time,
                        const Context &context, double angle) const
{
    if (nodes.isEmpty())
        {
            return;
        }
    painter.save();
    painter.translate(origin);
    painter.rotate(angle);
    for (const Node &node : nodes)
        {
            draw(painter, node, time, context);
        }
    painter.restore();
}

void BoundGauge::draw(QPainter &painter, const Node &node, double time, const Context &parentContext) const
{
    // parts inside this one are drawn in coordinates of their own
    Context context = parentContext;
    context.topLevel = false;
    const GaugeComponent &component = node.component;
    const Metric &metric = m_metrics.at(node.metric);
    bool known = false;
    double value = 0.0;
    if (metric.channel)
        {
            value = metric.display.convert(metric.channel->valueAt(time, &known));
        }
    if (component.hideWhenUnknown && !known)
        {
            return;
        }

    // Clips are made in the component's own coordinates, which earlier
    // manipulations (a group turning or sliding) move around. Measured from
    // the gauge's middle, three of its sizes cover anything that can show.
    const double reach = 3.0 * std::max(m_size.width(), m_size.height());
    painter.save();
    for (int i = 0; i < component.manipulations.size(); ++i)
        {
            const GaugeManipulation &m = component.manipulations.at(i);
            // without a value everything rests at 0, or at the end of its scale nearest to it:
            // a grade or g-force scale around 0 sits level, a speedometer at its start
            const double f = fraction(m, metric.range, known ? value : 0.0);
            if (!m.clipRect.isNull())
                {
                    painter.setClipRect(m.clipRect, Qt::IntersectClip);
                }
            switch (m.type)
                {
                case GaugeManipulation::Rotate:
                    painter.translate(m.pivot);
                    painter.rotate(lerp(m.minAngle, m.maxAngle, f));
                    painter.translate(-m.pivot);
                    break;
                case GaugeManipulation::Translate:
                    painter.translate(lerp(m.minX, m.maxX, f), lerp(m.minY, m.maxY, f));
                    break;
                case GaugeManipulation::RadialClip:
                {
                    const double sweep = lerp(m.minAngle, m.maxAngle, f) - m.minAngle;
                    if (std::fabs(sweep) < 360.0)
                        {
                            painter.setClipPath(slice(m.pivot, m.minAngle, sweep, reach), Qt::IntersectClip);
                        }
                    break;
                }
                case GaugeManipulation::StaticRadialClip:
                    if (std::fabs(m.maxAngle - m.minAngle) < 360.0)
                        {
                            painter.setClipPath(slice(m.pivot, m.minAngle, m.maxAngle - m.minAngle, reach), Qt::IntersectClip);
                        }
                    break;
                case GaugeManipulation::WidthClip:
                    painter.setClipPath(verticalBand(m.minX, lerp(m.minX, m.maxX, f), component.frame.center().y(), m.clipAngle, reach),
                                        Qt::IntersectClip);
                    break;
                case GaugeManipulation::HeightClip:
                    painter.setClipPath(horizontalBand(m.minY, lerp(m.minY, m.maxY, f), component.frame.center().x(), m.clipAngle, reach),
                                        Qt::IntersectClip);
                    break;
                case GaugeManipulation::Translate2D:
                {
                    // each axis: -1..1 over the largest value either way, then held inside the circle
                    auto axis = [&](int index)
                    {
                        if (index < 0 || !m_metrics.at(index).channel)
                            {
                                return 0.0;
                            }
                        const Metric &axisMetric = m_metrics.at(index);
                        bool ok = false;
                        const double v = axisMetric.display.convert(axisMetric.channel->valueAt(time, &ok));
                        const double extent = m.hasValueRange ? std::max(std::fabs(m.minValue), std::fabs(m.maxValue))
                                              : std::max(std::fabs(axisMetric.range.min), std::fabs(axisMetric.range.max));
                        return ok && extent > 0.0 ? std::clamp(v / extent, -1.0, 1.0) : 0.0;
                    };
                    double x = axis(node.axes.at(i).first) * (m.invertX ? -1.0 : 1.0);
                    double y = -axis(node.axes.at(i).second) * (m.invertY ? -1.0 : 1.0);
                    const double length = std::hypot(x, y);
                    if (length > 1.0)
                        {
                            x /= length;
                            y /= length;
                        }
                    painter.translate(x * m.maxRadius, y * m.maxRadius);
                    break;
                }
                case GaugeManipulation::AnimationStrip:
                {
                    if (m.frames < 1 || m.frameRect.isEmpty())
                        {
                            break;
                        }
                    const double phase = std::floor(phaseAt(node, m, time));
                    const int index = int(phase - std::floor(phase / m.frames) * m.frames);
                    painter.setClipRect(m.frameRect, Qt::IntersectClip);
                    // a strip runs along its longer side
                    if (component.frame.width() >= component.frame.height())
                        {
                            painter.translate(-index * m.frameRect.width(), 0);
                        }
                    else
                        {
                            painter.translate(0, -index * m.frameRect.height());
                        }
                    break;
                }
                }
        }

    switch (component.type)
        {
        case GaugeComponent::Image:
            if (!component.svg.isEmpty())
                {
                    drawSvg(painter, node);
                }
            else if (!component.image.isNull())
                {
                    painter.drawImage(component.frame, component.image);
                }
            break;
        case GaugeComponent::Text:
            drawText(painter, node, metric, known, value, time);
            break;
        case GaugeComponent::Shape:
            drawShape(painter, node);
            break;
        case GaugeComponent::Dial:
            drawDial(painter, node);
            break;
        case GaugeComponent::Tape:
            drawTape(painter, node, known, value);
            break;
        case GaugeComponent::Group:
            for (const Node &child : node.children)
                {
                    draw(painter, child, time, context);
                }
            break;
        case GaugeComponent::Graph:
            // a graph at the top, unmoved: its line can come from the cache
            drawGraph(painter, node, known, value, time, component.manipulations.isEmpty() ? parentContext : context);
            break;
        case GaugeComponent::TrackMap:
        case GaugeComponent::LapMap:
            // a map at the top, unmoved: its line can come from the cache
            drawRoute(painter, node, time, component.manipulations.isEmpty() ? parentContext : context);
            break;
        }
    painter.restore();
}

void BoundGauge::drawGraph(QPainter &painter, const Node &node, bool known, double value, double time, const Context &context) const
{
    if (node.line.isEmpty())
        {
            return;
        }
    const QRectF frame = node.component.frame;
    const GaugeComponent &c = node.component;
    if (c.windowBefore > 0.0 || c.windowAfter > 0.0)
        {
            // a window that moves along: the line changes with every frame
            const Metric &metric = m_metrics.at(node.metric);
            const double t0 = time - c.windowBefore;
            const double t1 = time + c.windowAfter;
            const int points = std::clamp(int(frame.width()), 50, 500);
            QVector<QPointF> line;
            line.reserve(points);
            for (int i = 0; i < points; ++i)
                {
                    const double t = lerp(t0, t1, double(i) / (points - 1));
                    bool ok = false;
                    const double v = metric.display.convert(metric.channel->valueAt(t, &ok));
                    if (ok)
                        {
                            line.append(QPointF(lerp(frame.left(), frame.right(), double(i) / (points - 1)),
                                                frame.bottom() - (v - node.graphV0) / (node.graphV1 - node.graphV0) * frame.height()));
                        }
                }
            if (line.size() >= 2)
                {
                    const QPainterPath path = polyline(line);
                    if (node.fill.isValid())
                        {
                            QPainterPath area = path;
                            area.lineTo(line.last().x(), frame.bottom());
                            area.lineTo(line.first().x(), frame.bottom());
                            area.closeSubpath();
                            painter.fillPath(area, node.fill);
                        }
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(node.color, c.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::BevelJoin));
                    painter.drawPath(path);
                }
            if (known)
                {
                    const double x = lerp(frame.left(), frame.right(), c.windowBefore / (c.windowBefore + c.windowAfter));
                    const double y = frame.bottom() - (value - node.graphV0) / (node.graphV1 - node.graphV0) * frame.height();
                    drawAt(painter, node.xMarker, QPointF(x, frame.top()), time, context);
                    drawAt(painter, node.yMarker, QPointF(frame.left(), y), time, context);
                    drawAt(painter, node.knob, QPointF(x, y), time, context);
                }
            return;
        }
    if (context.cached && context.topLevel)
        {
            // the painter stands at the gauge's corner, scaled to its size: the picture fits exactly
            painter.drawImage(QRectF(QPointF(0, 0), m_size), linePicture(node, 0, context));
        }
    else
        {
            drawLine(painter, node, 0);
        }
    if (!known)
        {
            return;
        }
    const double x = lerp(frame.left(), frame.right(), (time - node.graphT0) / (node.graphT1 - node.graphT0));
    const double y = frame.bottom() - (value - node.graphV0) / (node.graphV1 - node.graphV0) * frame.height();
    drawAt(painter, node.xMarker, QPointF(x, frame.top()), time, context);
    drawAt(painter, node.yMarker, QPointF(frame.left(), y), time, context);
    drawAt(painter, node.knob, QPointF(x, y), time, context);
}

void BoundGauge::drawLine(QPainter &painter, const Node &node, int route) const
{
    if (node.component.type == GaugeComponent::Graph && node.fill.isValid())
        {
            painter.fillPath(node.area, node.fill);
        }
    painter.setBrush(Qt::NoBrush);
    // bevel joins: the same on a line this thin, and round ones cost an arc at every corner
    painter.setPen(QPen(node.color, node.component.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::BevelJoin));
    painter.drawPath(node.component.type == GaugeComponent::Graph ? node.line : node.routes.at(route).path);
}

QImage BoundGauge::linePicture(const Node &node, int route, const Context &context) const
{
    const QString key = QString("%1/%2/%3x%4@%5").arg(node.lineSlot).arg(route)
                        .arg(context.box.width()).arg(context.box.height()).arg(context.devicePixelRatio);
    {
        const QMutexLocker lock(&m_cache->mutex);
        const auto found = m_cache->lines.constFind(key);
        if (found != m_cache->lines.constEnd())
            {
                return found.value();
            }
    }
    QImage picture(context.box * context.devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
    picture.setDevicePixelRatio(context.devicePixelRatio);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(context.box.width() / m_size.width(), context.box.height() / m_size.height());
    painter.setClipRect(QRectF(QPointF(0, 0), m_size));
    drawLine(painter, node, route);
    painter.end();
    const QMutexLocker lock(&m_cache->mutex);
    // sizes come and go while a gauge is resized; the pictures need not pile up
    if (m_cache->lines.size() > 64)
        {
            m_cache->lines.clear();
        }
    m_cache->lines.insert(key, picture);
    return picture;
}

void BoundGauge::drawRoute(QPainter &painter, const Node &node, double time, const Context &context) const
{
    if (node.routes.isEmpty())
        {
            return;
        }
    // the lap under way; before the first one the first, after the last one the last
    int index = 0;
    for (int i = 0; i < node.routes.size(); ++i)
        {
            if (node.routes.at(i).startTime <= time)
                {
                    index = i;
                }
        }
    const Route &route = node.routes.at(index);
    if (context.cached && context.topLevel)
        {
            // the painter stands at the gauge's corner, scaled to its size: the picture fits exactly
            painter.drawImage(QRectF(QPointF(0, 0), m_size), linePicture(node, index, context));
        }
    else
        {
            drawLine(painter, node, index);
        }
    drawAt(painter, node.startLine, route.startPoint, time, context, route.startAngle);
    QPointF position;
    if (positionAt(time, &position))
        {
            drawAt(painter, node.knob, route.toFrame.map(position), time, context);
        }
}

QString BoundGauge::clockText(const GaugeComponent &component, double time) const
{
    if (!m_session || !m_session->startUtc().isValid())
        {
            return QStringLiteral("--");
        }
    QDateTime moment = m_session->startUtc().addMSecs(qint64(std::llround(time * 1000.0)));
    if (component.localTime)
        {
            moment = m_session->hasUtcOffset() ? moment.toOffsetFromUtc(m_session->utcOffsetSeconds()) : moment.toLocalTime();
        }
    return moment.toString(component.clockFormat.isEmpty() ? QStringLiteral("HH:mm:ss") : component.clockFormat);
}

void BoundGauge::drawText(QPainter &painter, const Node &node, const Metric &metric, bool known, double value, double time) const
{
    const GaugeComponent &component = node.component;
    const int decimals = component.precision >= 0 ? component.precision : metric.display.decimals;
    if (component.clampValue)
        {
            value = std::clamp(value, component.clampMin, std::max(component.clampMin, component.clampMax));
        }
    QString number = QStringLiteral("--");
    if (component.valueFormat == GaugeComponent::Clock)
        {
            number = clockText(component, time);
        }
    else if (known)
        {
            const QString sign = component.showPlus && value >= 0.0 ? QStringLiteral("+") : QString();
            switch (component.valueFormat)
                {
                case GaugeComponent::WholeNumber:
                    number = sign + QString::number(qint64(std::trunc(value)));
                    break;
                case GaugeComponent::Duration:
                {
                    const double seconds = std::fabs(value);
                    const int width = decimals > 0 ? decimals + 3 : 2;
                    number = (value < 0.0 ? QStringLiteral("-") : sign) + QString::number(qint64(seconds / 60.0)) + ':'
                             + QString("%1").arg(std::fmod(seconds, 60.0), width, 'f', std::max(0, decimals), QLatin1Char('0'));
                    break;
                }
                case GaugeComponent::DegreesMinutesSeconds:
                {
                    const qint64 degrees = qint64(std::trunc(value));
                    const double minutes = (std::fabs(value) - std::fabs(double(degrees))) * 60.0;
                    const double seconds = (minutes - std::trunc(minutes)) * 60.0;
                    number = QString("%1° %2' %3\"").arg(degrees).arg(qint64(minutes)).arg(seconds, 0, 'f', std::max(0, decimals));
                    break;
                }
                default:
                    number = sign + QString::number(value, 'f', decimals);
                    break;
                }
        }
    QString text;
    switch (component.autoText)
        {
        case GaugeComponent::FixedText:
            text = component.text;
            break;
        case GaugeComponent::Value:
            text = number;
            break;
        case GaugeComponent::Unit:
            text = metric.display.unit;
            break;
        case GaugeComponent::ValueAndUnit:
            text = number + " " + metric.display.unit;
            break;
        case GaugeComponent::Label:
            text = metric.display.name;
            break;
        case GaugeComponent::LabelValueUnit:
            text = metric.display.name + " " + number + " " + metric.display.unit;
            break;
        case GaugeComponent::IntervalMarking:
            text = metric.markings.value(node.marking);
            break;
        }
    if (component.upperCase)
        {
            text = text.toUpper();
        }
    if (text.isEmpty())
        {
            return;
        }

    painter.setFont(fontOf(painter, component));
    painter.setPen(node.color);
    if (component.boxed)
        {
            painter.drawText(component.frame, int(component.alignment) | Qt::TextWordWrap, text);
            return;
        }
    // the frame's corner is the anchor; the alignment says which point of the text sits on it
    drawAnchored(painter, text, component.frame.topLeft(), component.alignment);
}

// Drawn as lines and curves at whatever size it ends up, so it is sharp in a
// 4K export too. With a colour of its own it is one shape in that colour, as
// a picture would be: then it is drawn at the size it covers on the device.
void BoundGauge::drawSvg(QPainter &painter, const Node &node) const
{
    QSvgRenderer *renderer = svgRenderer(node.component.svg);
    if (!renderer->isValid())
        {
            return;
        }
    const QRectF &frame = node.component.frame;
    if (node.component.color.role == GaugeColor::Original)
        {
            renderer->render(&painter, frame);
            return;
        }
    const double scale = std::sqrt(std::fabs(painter.worldTransform().determinant()))
                         * (painter.device() ? painter.device()->devicePixelRatioF() : 1.0);
    const QSize size(std::max(1, int(std::ceil(frame.width() * scale))), std::max(1, int(std::ceil(frame.height() * scale))));
    if (size.width() > 8192 || size.height() > 8192)
        {
            return;
        }
    QImage picture(size, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::transparent);
    {
        QPainter drawing(&picture);
        drawing.setRenderHint(QPainter::Antialiasing);
        renderer->render(&drawing);
    }
    painter.drawImage(frame, tinted(picture, node.color));
}

void BoundGauge::drawShape(QPainter &painter, const Node &node) const
{
    const GaugeComponent &c = node.component;
    if (node.shape.isEmpty())
        {
            return;
        }
    if (c.filled)
        {
            if (node.gradient.isValid() && c.gradientLine.length() > 0.0)
                {
                    QLinearGradient gradient(c.gradientLine.p1(), c.gradientLine.p2());
                    gradient.setColorAt(0.0, node.color);
                    gradient.setColorAt(1.0, node.gradient);
                    painter.fillPath(node.shape, gradient);
                }
            else
                {
                    painter.fillPath(node.shape, node.color);
                }
            return;
        }
    if (setPen(painter, node.color, c.lineWidth))
        {
            painter.drawPath(node.shape);
        }
}

void BoundGauge::drawDial(QPainter &painter, const Node &node) const
{
    const GaugeComponent &c = node.component;
    const GaugeScale &s = c.scale;
    const QPointF centre = c.frame.center();
    if (s.arcWidth > 0.0 && setPen(painter, node.arcColor, s.arcWidth))
        {
            const QRectF circle(centre.x() - s.radius, centre.y() - s.radius, 2.0 * s.radius, 2.0 * s.radius);
            QPainterPath arc;
            arc.arcMoveTo(circle, qtAngle(s.startAngle));
            arc.arcTo(circle, qtAngle(s.startAngle), -(s.endAngle - s.startAngle));
            painter.drawPath(arc);
        }
    // ticks reach in from the arc's inner edge
    const double edge = s.radius - s.arcWidth / 2.0;
    painter.setFont(fontOf(painter, c));
    for (const GaugeTicks *ticks : {&s.minor, &s.major})
        {
            if (ticks->unit <= 0.0 || s.maxValue < s.minValue)
                {
                    continue;
                }
            const QColor &color = ticks == &s.major ? node.majorColor : node.minorColor;
            const bool drawn = setPen(painter, color, ticks->width);
            // counted, not summed, so the last tick is not lost to rounding
            const int count = int(std::floor((s.maxValue - s.minValue) / ticks->unit + 1e-9));
            for (int i = 0; i <= std::min(count, 10000); ++i)
                {
                    const double value = s.minValue + i * ticks->unit;
                    const double f = s.maxValue > s.minValue ? (value - s.minValue) / (s.maxValue - s.minValue) : 0.0;
                    const double angle = lerp(s.startAngle, s.endAngle, f) * kPi / 180.0;
                    const QPointF direction(std::sin(angle), -std::cos(angle));
                    if (drawn)
                        {
                            painter.drawLine(centre + direction * (edge - ticks->length), centre + direction * edge);
                        }
                    if (ticks == &s.major && node.color.alpha() > 0)
                        {
                            painter.setPen(node.color);
                            drawAnchored(painter, scaleNumber(value), centre + direction * (s.radius - s.labelOffset), c.alignment);
                            setPen(painter, color, ticks->width);
                        }
                }
        }
}

void BoundGauge::drawTape(QPainter &painter, const Node &node, bool known, double value) const
{
    const GaugeComponent &c = node.component;
    const GaugeScale &s = c.scale;
    if (s.major.unit <= 0.0 || s.spacing <= 0.0 || s.maxValue <= s.minValue)
        {
            return;
        }
    if (!known)
        {
            value = std::clamp(0.0, s.minValue, s.maxValue);
        }
    else if (s.loop)
        {
            value = s.minValue + std::fmod(std::fmod(value - s.minValue, s.maxValue - s.minValue) + (s.maxValue - s.minValue),
                                           s.maxValue - s.minValue);
        }
    else
        {
            value = std::clamp(value, s.minValue, s.maxValue);
        }
    painter.setFont(fontOf(painter, c));
    const QFontMetricsF metrics(painter.font());
    // the scale starts half a number in, so the end numbers fit
    const double labelWidth = std::max(metrics.horizontalAdvance(scaleNumber(s.minValue)),
                                       metrics.horizontalAdvance(scaleNumber(s.maxValue)));
    const double labelHeight = metrics.height();
    const bool horizontal = s.orientation == GaugeScale::NumbersAbove || s.orientation == GaugeScale::NumbersBelow;
    const double thickness = std::max(s.markerLength, std::max(s.major.length + s.labelOffset
                                      + (horizontal ? labelHeight : labelWidth), s.minor.length));
    const QPointF origin = c.frame.topLeft();
    const double start = (horizontal ? labelWidth : labelHeight) / 2.0;
    const double valuesPerPixel = s.major.unit / s.spacing;
    const double first = value - s.length / 2.0 * valuesPerPixel;

    for (const GaugeTicks *ticks : {&s.minor, &s.major})
        {
            if (ticks->unit <= 0.0)
                {
                    continue;
                }
            const QColor &color = ticks == &s.major ? node.majorColor : node.minorColor;
            const bool drawn = setPen(painter, color, ticks->width);
            const double firstTick = std::ceil(first / ticks->unit);
            const double step = ticks->unit / valuesPerPixel;
            for (int i = 0; i < 10000; ++i)
                {
                    const double along = (firstTick * ticks->unit - first) / valuesPerPixel + i * step;
                    if (along > s.length)
                        {
                            break;
                        }
                    double tick = (firstTick + i) * ticks->unit;
                    if (s.loop)
                        {
                            const double span = s.maxValue - s.minValue;
                            tick = s.minValue + std::fmod(std::fmod(tick - s.minValue, span) + span, span);
                        }
                    else if (tick < s.minValue - 1e-9 || tick > s.maxValue + 1e-9)
                        {
                            continue;
                        }
                    QPointF from;
                    QPointF to;
                    QPointF label;
                    if (horizontal)
                        {
                            const double x = origin.x() + start + (s.reverse ? s.length - along : along);
                            const bool above = s.orientation == GaugeScale::NumbersAbove;
                            const double base = above ? origin.y() + thickness : origin.y();
                            const double sign = above ? -1.0 : 1.0;
                            from = QPointF(x, base);
                            to = QPointF(x, base + sign * ticks->length);
                            label = QPointF(x, base + sign * (ticks->length + s.labelOffset));
                        }
                    else
                        {
                            // values grow upwards unless reversed
                            const double y = origin.y() + start + (s.reverse ? along : s.length - along);
                            const bool left = s.orientation == GaugeScale::NumbersLeft;
                            const double base = left ? origin.x() + thickness : origin.x();
                            const double sign = left ? -1.0 : 1.0;
                            from = QPointF(base, y);
                            to = QPointF(base + sign * ticks->length, y);
                            label = QPointF(base + sign * (ticks->length + s.labelOffset), y);
                        }
                    if (drawn)
                        {
                            painter.drawLine(from, to);
                        }
                    if (ticks == &s.major && node.color.alpha() > 0)
                        {
                            painter.setPen(node.color);
                            drawAnchored(painter, scaleNumber(tick), label, c.alignment);
                            setPen(painter, color, ticks->width);
                        }
                }
        }
    if (s.markerLength > 0.0 && setPen(painter, node.markerColor, s.markerWidth))
        {
            const double middle = start + s.length / 2.0;
            switch (s.orientation)
                {
                case GaugeScale::NumbersAbove:
                    painter.drawLine(QPointF(origin.x() + middle, origin.y() + thickness),
                                     QPointF(origin.x() + middle, origin.y() + thickness - s.markerLength));
                    break;
                case GaugeScale::NumbersBelow:
                    painter.drawLine(QPointF(origin.x() + middle, origin.y()), QPointF(origin.x() + middle, origin.y() + s.markerLength));
                    break;
                case GaugeScale::NumbersLeft:
                    painter.drawLine(QPointF(origin.x() + thickness, origin.y() + middle),
                                     QPointF(origin.x() + thickness - s.markerLength, origin.y() + middle));
                    break;
                case GaugeScale::NumbersRight:
                    painter.drawLine(QPointF(origin.x(), origin.y() + middle), QPointF(origin.x() + s.markerLength, origin.y() + middle));
                    break;
                }
        }
}
