#include "gaugerenderer.h"

#include "../maps/tilestore.h"
#include "../telemetry/channelids.h"
#include "channelicons.h"
#include "gaugefonts.h"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPaintEngine>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

namespace
{
const double kPi = 3.14159265358979323846;
// most points a graph line or a route is drawn with
const int kMaxPathPoints = 2000;
// a map that follows the rider shows a short stretch large: more points, drawn in runs of this many
const int kMaxFollowPoints = 20000;
const int kRunLength = 64;
// tile pixels across what such a map shows: sharp on a gauge a quarter of a 4K frame high
const double kFollowPixels = 512.0;
const double kMetersPerDegree = 111320.0;

// What a map that follows the rider shows around a point, in its frame's units: the frame,
// or - turned to the way ahead - the square the frame fits into at any angle. And the tile
// pixels across that, so that both come from tiles of the same zoom.
QSizeF followSize(const GaugeComponent &component)
{
    const QSizeF frame = component.frame.size();
    const double diagonal = std::hypot(frame.width(), frame.height());
    return component.headingUp ? QSizeF(diagonal, diagonal) : frame;
}

double followPixels(const GaugeComponent &component)
{
    const double longer = std::max(component.frame.width(), component.frame.height());
    return component.headingUp ? kFollowPixels * followSize(component).width() / longer : kFollowPixels;
}

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

// A drawing with the theme's colors put in for @foreground, @background,
// @accent and @accent2, and their transparency for @foreground-opacity and
// so on (the SVG renderer reads no transparency in a color itself).
QByteArray themedSvg(QByteArray svg, const GaugeTheme &theme)
{
    // texts in a drawing follow the theme's font as other texts do
    if (!theme.font.isEmpty())
        {
            svg.replace(QByteArray("font-family='") + GaugeFonts::Family + "'", "font-family='" + theme.font.toUtf8() + "'");
        }
    // bold in our drawings is semibold, as in our texts, unless the theme says
    svg.replace("font-weight='bold'", "font-weight='" + QByteArray::number(theme.fontWeight > 0 ? theme.fontWeight : 600) + "'");
    const QPair<const char *, QColor> colors[] = {{"foreground", theme.foreground}, {"background", theme.background},
        {"accent2", theme.accent2}, {"accent", theme.accent}
    };
    // the longer names first: "@accent" is the start of "@accent2"
    for (const auto &color : colors)
        {
            const QByteArray name = QByteArray("@") + color.first;
            svg.replace(name + "-opacity", QByteArray::number(color.second.alphaF(), 'f', 3));
            svg.replace(name, color.second.name(QColor::HexRgb).toLatin1());
        }
    return svg;
}

// Parsing a drawing for every frame would cost more than drawing it. Each
// thread keeps its own: preview and export draw at the same time.
// Looked up by the drawing's buffer, not its text: hashing kilobytes of SVG for every part of
// every frame cost more than drawing some. The cache keeps a copy of each drawing, so a buffer
// it knows cannot be freed and come back holding another.
struct CachedSvg
{
    QByteArray svg;
    std::shared_ptr<QSvgRenderer> renderer;
};

QSvgRenderer *svgRenderer(const QByteArray &svg)
{
    thread_local QHash<const char *, CachedSvg> cache;
    auto found = cache.find(svg.constData());
    if (found == cache.end() || found->svg.size() != svg.size())
        {
            if (cache.size() > 200)
                {
                    cache.clear();
                }
            found = cache.insert(svg.constData(), {svg, std::make_shared<QSvgRenderer>(svg)});
        }
    return found->renderer.get();
}

// A drawing of one color at one size, made once: a needle turns, but stays the same picture.
QImage tintedSvg(QSvgRenderer *renderer, const QByteArray &svg, const QSize &size, const QColor &color)
{
    struct Key
    {
        const char *svg;
        int width;
        int height;
        QRgb color;
        bool operator==(const Key &other) const
        {
            return svg == other.svg && width == other.width && height == other.height && color == other.color;
        }
    };
    struct KeyHash
    {
        size_t operator()(const Key &key) const
        {
            return qHashMulti(0, key.svg, key.width, key.height, key.color);
        }
    };
    // each with its drawing, so that the buffer it is known by stays that drawing's
    thread_local std::unordered_map<Key, std::pair<QByteArray, QImage>, KeyHash> cache;
    const Key key{svg.constData(), size.width(), size.height(), color.rgba()};
    const auto found = cache.find(key);
    if (found != cache.end() && found->second.first.size() == svg.size())
        {
            return found->second.second;
        }
    QImage picture(size, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::transparent);
    {
        QPainter drawing(&picture);
        drawing.setRenderHint(QPainter::Antialiasing);
        renderer->render(&drawing);
        drawing.setCompositionMode(QPainter::CompositionMode_SourceIn);
        drawing.fillRect(picture.rect(), color);
    }
    if (cache.size() > 64)
        {
            cache.clear();
        }
    cache[key] = {svg, picture};
    return picture;
}

// "25 km/h", but "204°": a plain degree sign sits on the number.
QString withUnit(const QString &number, const QString &unit)
{
    if (unit.isEmpty())
        {
            return number;
        }
    return unit == QStringLiteral("°") ? number + unit : number + QLatin1Char(' ') + unit;
}

// Gauge artwork is a shape, not a picture: only its transparency is kept
// and the color comes from the theme.
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
// The way a moving part went over the last `seconds` before `time`, around `center`: a line
// in its color, as wide as `width` at the present and thinner and fainter towards the past.
void drawTrail(QPainter &painter, const QPointF &center, const QColor &color, double width, double seconds,
               double time, const std::function<QPointF(double)> &offsetAt)
{
    // a step a twentieth of a second: smooth enough, and data comes no finer
    const int steps = std::clamp(int(std::lround(seconds * 20.0)), 2, 600);
    QPointF previous = center + offsetAt(time - seconds);
    painter.save();
    for (int k = 1; k <= steps; ++k)
        {
            const double share = double(k) / steps;     // 0 in the past, 1 now
            const QPointF point = center + offsetAt(time - seconds * (1.0 - share));
            QColor faded = color;
            faded.setAlphaF(color.alphaF() * 0.75 * share);
            painter.setPen(QPen(faded, std::max(1.0, width * (0.25 + 0.75 * share)), Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(previous, point);
            previous = point;
        }
    painter.restore();
}

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

// The font a text is drawn in: as the gauge says, unless the theme says otherwise.
QFont fontOf(const QPainter &painter, const GaugeComponent &c, const GaugeTheme &theme)
{
    QFont font = painter.font();
    QString family = c.fontFamily;
    // gauges naming a font of their own keep it, and its weights, unless told otherwise
    const bool themed = family.isEmpty() || family == QLatin1String(GaugeFonts::Family) || theme.fontEverywhere;
    if (!theme.font.isEmpty() && themed)
        {
            family = theme.font;
        }
    if (!family.isEmpty())
        {
            font.setFamily(family);
        }
    font.setPixelSize(std::max(1, int(std::lround(c.fontSize))));
    font.setItalic(c.italic);
    if (theme.fontWeight > 0 && themed)
        {
            font.setWeight(QFont::Weight(theme.fontWeight));
        }
    else if (family == QLatin1String(GaugeFonts::Family))
        {
            // our gauges are designed in medium, and semibold for bold
            font.setWeight(c.bold ? QFont::DemiBold : QFont::Medium);
        }
    else
        {
            font.setBold(c.bold);
        }
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
    double baseline = anchor.y() + (metrics.ascent() - metrics.descent()) / 2.0; // centerd
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
                       const Units &units, const GaugeTheme &theme, const Choices &choices)
    : m_size(gauge.size)
    , m_channel(channel)
    , m_choices(choices)
    , m_session(session)
    , m_theme(theme)
{
    // before any text of a gauge is measured
    GaugeFonts::load();
    if (m_session)
        {
            m_lat = m_session->channel(ChannelId::PositionLat);
            m_lon = m_session->channel(ChannelId::PositionLon);
            m_heading = m_session->channel(ChannelId::Heading);
            if (!m_lat || !m_lon || m_lat->isEmpty())
                {
                    m_lat = m_lon = nullptr;
                }
        }
    metricFor(QString(), units); // index 0: the bound channel
    m_nodes = bindAll(gauge.components, units, theme);
    prepareLaps();
    // scales can be set only now that every marking has been counted
    // Where the scale of the bound channel ends: where the user chose, or, for a dial that draws
    // numbers fixed when it was made (an imported 0-160 speedometer), a round number above the
    // highest value of the ride. Numbers printed in a picture cannot move: a gauge with no dial
    // of its own keeps its range.
    m_scaleEnd = m_choices.scaleMax;
    if (m_scaleEnd <= 0.0 && !m_metrics.isEmpty() && m_metrics.first().channel && !m_metrics.first().channel->isEmpty())
        {
            const Metric &bound = m_metrics.first();
            // a GPS spike would stretch the dial for one wrong reading
            const double highest = bound.display.convert(bound.channel->steadyMax(1.0));
            std::function<const GaugeScale *(const QVector<Node> &)> dialOf = [&](const QVector<Node> &nodes)
            {
                for (const Node &node : nodes)
                    {
                        const GaugeScale &scale = node.component.scale;
                        if (node.metric == 0 && node.component.type == GaugeComponent::Dial && !scale.loop
                                && scale.major.unit > 0.0 && scale.maxValue > scale.minValue)
                            {
                                return &scale;
                            }
                        if (const GaugeScale *inside = dialOf(node.children))
                            {
                                return inside;
                            }
                    }
                return static_cast<const GaugeScale *>(nullptr);
            };
            if (const GaugeScale *dial = dialOf(m_nodes); dial && highest > dial->minValue)
                {
                    const int majors = std::max(1, int(std::lround((dial->maxValue - dial->minValue) / dial->major.unit)));
                    m_scaleEnd = MetricRange::covering(dial->minValue, highest, majors + 1).max;
                }
        }
    const bool ownEnd = m_choices.scaleMax > 0.0;
    const bool movedEnd = m_scaleEnd > 0.0;
    for (int i = 0; i < m_metrics.size(); ++i)
        {
            Metric &metric = m_metrics[i];
            const bool hasData = metric.channel && !metric.channel->isEmpty();
            metric.range = hasData
                           ? MetricRange::covering(metric.display.convert(metric.channel->minValue()),
                                                   metric.display.convert(metric.channel->maxValue()), metric.markingCount)
                           : MetricRange::covering(0.0, 100.0, metric.markingCount);
            if (i == 0 && ownEnd && m_choices.scaleMax > metric.range.min)
                {
                    // the numbers printed are as many as the gauge has places for
                    const int intervals = metric.markingCount >= 2 ? metric.markingCount - 1 : 4;
                    metric.range = {metric.range.min, m_choices.scaleMax, (m_choices.scaleMax - metric.range.min) / intervals};
                }
            metric.markings = metric.range.markingTexts(metric.markingCount);
        }
    // these need the scales
    std::function<void(QVector<Node> &)> finish = [&](QVector<Node> &nodes)
    {
        for (Node &node : nodes)
            {
                // a tape slides by its numbers' spacing and has no end to move
                if (movedEnd && node.metric == 0 && node.component.type == GaugeComponent::Dial)
                    {
                        rescale(node.component.scale);
                    }
                for (GaugeManipulation &m : node.component.manipulations)
                    {
                        // a needle drawn for a scale of its own goes to the new end too
                        if (movedEnd && node.metric == 0 && m.hasValueRange && m.curve.isEmpty() && !m.wrapValue
                                && m_scaleEnd > m.minValue)
                            {
                                m.maxValue = m_scaleEnd;
                            }
                        const Metric &metric = m_metrics.at(node.metric);
                        if (m.recordingRange && metric.channel && !metric.channel->isEmpty())
                            {
                                m.hasValueRange = true;
                                m.minValue = metric.display.convert(metric.channel->minValue());
                                m.maxValue = metric.display.convert(metric.channel->maxValue());
                            }
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
                for (QVector<Node> *list : {&node.children, &node.knob, &node.knobTurning, &node.xMarker, &node.yMarker, &node.startLine})
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

namespace
{
// The value rounded to `decimals` of its smallest unit (seconds, of time or of arc) first and
// only then split, so that 119.97 s is 2:00.0, not 1:60.0.
struct Split
{
    bool negative{false};
    qint64 whole{0};      // minutes, degrees
    qint64 middle{0};     // arc minutes; unused for a duration
    double last{0.0};     // seconds
};

// `secondsPerValue`: seconds in one of the value's units (1 for a duration, 3600 for degrees);
// `secondsPerWhole`: seconds in the first part shown (60 in a minute, 3600 in a degree).
Split split(double value, double secondsPerValue, double secondsPerWhole, bool withMiddle, int decimals)
{
    const int places = std::clamp(decimals, 0, 6);
    const double step = std::pow(10.0, places);
    const qint64 total = qint64(std::llround(std::fabs(value) * secondsPerValue * step));
    const qint64 perWhole = qint64(std::llround(secondsPerWhole * step));
    const qint64 perMiddle = qint64(std::llround(60.0 * step));
    Split parts;
    parts.negative = value < 0.0 && total > 0;   // rounded to nothing: no minus
    parts.whole = total / perWhole;
    qint64 rest = total % perWhole;
    if (withMiddle)
        {
            parts.middle = rest / perMiddle;
            rest %= perMiddle;
        }
    parts.last = double(rest) / step;
    return parts;
}
}

QString BoundGauge::durationText(double value, int decimals, const QString &plus)
{
    const Split parts = split(value, 1.0, 60.0, false, decimals);
    const int places = std::clamp(decimals, 0, 6);
    const int width = places > 0 ? places + 3 : 2;
    return (parts.negative ? QStringLiteral("-") : plus) + QString::number(parts.whole) + ':'
           + QString("%1").arg(parts.last, width, 'f', places, QLatin1Char('0'));
}

QString BoundGauge::degreesText(double value, int decimals)
{
    // a place west or south of zero but within a degree keeps its sign: -0° 7' 12"
    const Split parts = split(value, 3600.0, 3600.0, true, decimals);
    return QString("%1%2° %3' %4\"").arg(parts.negative ? QStringLiteral("-") : QString()).arg(parts.whole)
           .arg(parts.middle).arg(parts.last, 0, 'f', std::clamp(decimals, 0, 6));
}

QString BoundGauge::shownChannel(const QString &channel) const
{
    return channel.isEmpty() ? m_channel : m_choices.channels.value(channel, channel);
}

void BoundGauge::rescale(GaugeScale &scale) const
{
    if (scale.loop || m_scaleEnd <= scale.minValue || scale.major.unit <= 0.0 || scale.maxValue <= scale.minValue)
        {
            return;
        }
    // about as many numbers as before, at round values
    const int majors = std::max(1, int(std::lround((scale.maxValue - scale.minValue) / scale.major.unit)));
    const MetricRange range = MetricRange::upTo(scale.minValue, m_scaleEnd, majors);
    if (scale.minor.unit > 0.0)
        {
            const int perMajor = std::max(1, int(std::lround(scale.major.unit / scale.minor.unit)));
            scale.minor.unit = range.step / perMajor;
        }
    scale.major.unit = range.step;
    scale.maxValue = range.max;
}

int BoundGauge::metricFor(const QString &channel, const Units &units, const QString &unit)
{
    const QString id = shownChannel(channel);
    // the same channel in a unit of its own is a scale of its own
    const QString key = unit.isEmpty() ? id : id + QLatin1Char('\n') + unit;
    const auto known = m_metricIndex.constFind(key);
    if (known != m_metricIndex.constEnd())
        {
            return known.value();
        }
    Metric metric;
    metric.channel = m_session ? m_session->channel(id) : nullptr;
    metric.clock = id == ChannelId::TimeOfDay ? QStringLiteral("HH:mm:ss") : id == ChannelId::Date ? QStringLiteral("yyyy-MM-dd")
                   : id == ChannelId::DateAndTime ? QStringLiteral("yyyy-MM-dd HH:mm") : QString();
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
    const QString channelId = shownChannel(component.channel);
    if (component.type == GaugeComponent::Shape)
        {
            node.shape = shapePath(component);
            if (component.gradientColor.role != GaugeColor::Original)
                {
                    node.gradient = themeColor(component.gradientColor, theme);
                }
        }
    if (component.type == GaugeComponent::Gears || component.type == GaugeComponent::LapTable
            || component.type == GaugeComponent::SectorBar)
        {
            node.markerColor = themeColor(component.activeColor, theme);
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
    // north up: y grows downwards on screen. Mercator, as street maps are:
    // the true shape at any latitude, and map tiles fit under it
    *projected = QPointF(lon, -TileStore::mercatorLatitude(lat));
    return latOk && lonOk;
}

void BoundGauge::prepareRoutes(Node &node) const
{
    if (!m_lat || !m_lon || m_lat->count() < 2)
        {
            return;
        }
    if (node.component.span > 0.0 && node.component.headingUp)
        {
            // what shows the heading turns with the map
            QVector<Node> upright;
            for (const Node &part : std::as_const(node.knob))
                {
                    (part.component.channel == QLatin1String(ChannelId::Heading) ? node.knobTurning : upright).append(part);
                }
            node.knob = upright;
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
            const bool follows = node.component.span > 0.0;
            const int count = std::clamp(m_lat->count(), 2, follows ? kMaxFollowPoints : kMaxPathPoints);
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
            // keep the shape: one scale for both directions, centerd
            double scale = std::min(area.width() / std::max(bounds.width(), 1e-9),
                                    area.height() / std::max(bounds.height(), 1e-9));
            if (follows)
                {
                    // a projected degree is this long on the ground at the route's latitude
                    const double meters = kMetersPerDegree * std::cos(TileStore::latitudeOf(-bounds.center().y()) * kPi / 180.0);
                    scale = std::min(node.component.frame.width(), node.component.frame.height())
                            / (node.component.span / std::max(meters, 1.0));
                }
            Route route;
            route.follows = follows;
            route.toFrame.translate(area.center().x(), area.center().y());
            route.toFrame.scale(scale, scale);
            route.toFrame.translate(-bounds.center().x(), -bounds.center().y());
            QVector<QPointF> placed;
            placed.reserve(points.size());
            for (const QPointF &p : points)
                {
                    placed.append(route.toFrame.map(p));
                }
            if (follows)
                {
                    route.points = simplified(placed, kLineTolerance);
                    for (int from = 0; from + 1 < route.points.size(); from += kRunLength)
                        {
                            const int to = std::min(from + kRunLength, int(route.points.size()) - 1);
                            QRectF run(route.points.at(from), QSizeF(0, 0));
                            for (int i = from + 1; i <= to; ++i)
                                {
                                    const QPointF &p = route.points.at(i);
                                    run.setCoords(std::min(run.left(), p.x()), std::min(run.top(), p.y()),
                                                  std::max(run.right(), p.x()), std::max(run.bottom(), p.y()));
                                }
                            route.runBounds.append(run);
                        }
                    if (node.component.tiles)
                        {
                            // the tiles of the whole way, so that an export needs no connection:
                            // what the frame shows around points a third of it apart
                            const QSizeF shown = followSize(node.component);
                            const double step = std::min(node.component.frame.width(), node.component.frame.height()) / 3.0;
                            const QTransform toMap = route.toFrame.inverted();
                            QVector<QRectF> areas;
                            QPointF last;
                            for (const QPointF &p : std::as_const(route.points))
                                {
                                    if (areas.isEmpty() || std::hypot(p.x() - last.x(), p.y() - last.y()) >= step)
                                        {
                                            areas.append(toMap.mapRect(QRectF(p.x() - shown.width() / 2.0, p.y() - shown.height() / 2.0,
                                                                              shown.width(), shown.height())));
                                            last = p;
                                        }
                                }
                            TileStore::instance()->want(areas, followPixels(node.component));
                        }
                }
            else
                {
                    route.path = polyline(simplified(placed, kLineTolerance));
                }
            if (node.component.tiles && !follows)
                {
                    // what the frame shows, back on the map
                    const TileStore::Picture map = TileStore::instance()->picture(route.toFrame.inverted().mapRect(node.component.frame));
                    route.map = map.image;
                    route.mapRect = route.toFrame.mapRect(map.bounds);
                }
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
            const double fps = lerp(strip.minFps, strip.maxFps, fraction(strip, metric.range, value));
            phase += fps * (t - previous);
            node.phaseTimes.append(t);
            node.phases.append(phase);
            previous = t;
        }
}

double BoundGauge::phaseAt(const Node &node, const GaugeManipulation &strip, double time) const
{
    if (node.phaseTimes.isEmpty() || std::isnan(time))
        {
            return std::isnan(time) ? 0.0 : time * strip.minFps;
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
    // cached pictures only fit a painter that does not scale or turn them, and
    // one that paints pixels: drawn into an SVG or a PDF the gauge stays lines
    const QPaintEngine *engine = painter.paintEngine();
    const bool pixels = engine && (engine->type() == QPaintEngine::Raster || engine->type() == QPaintEngine::OpenGL2);
    Context context;
    context.box = box.size();
    context.cached = pixels && painter.worldTransform().type() <= QTransform::TxTranslate;
    context.devicePixelRatio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    context.topLevel = true;
    const QVector<QImage> layers = context.cached ? staticLayers(box.size(), context.devicePixelRatio) : QVector<QImage>();
    // OpenGL paints shapes smooth only on a multisampled surface: the moving parts are drawn
    // into a picture first, so the preview looks like the export
    const bool offscreen = context.cached && engine->type() == QPaintEngine::OpenGL2;

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
            if (offscreen)
                {
                    // one picture kept for this, not a new one of megabytes for every part every frame
                    thread_local QImage live;
                    const QSize size = box.size() * context.devicePixelRatio;
                    if (live.size() != size)
                        {
                            live = QImage(size, QImage::Format_ARGB32_Premultiplied);
                        }
                    live.setDevicePixelRatio(context.devicePixelRatio);
                    live.fill(Qt::transparent);
                    QPainter drawing(&live);
                    drawing.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
                    drawing.scale(box.width() / m_size.width(), box.height() / m_size.height());
                    drawing.setClipRect(QRectF(QPointF(0, 0), m_size));
                    for (int i = run.first; i < run.end; ++i)
                        {
                            draw(drawing, m_nodes.at(i), time, context);
                        }
                    drawing.end();
                    painter.drawImage(box.topLeft(), live);
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
                    auto axis = [&](int index, double when)
                    {
                        if (index < 0 || !m_metrics.at(index).channel)
                            {
                                return 0.0;
                            }
                        const Metric &axisMetric = m_metrics.at(index);
                        bool ok = false;
                        const double v = axisMetric.display.convert(axisMetric.channel->valueAt(when, &ok));
                        const double extent = m.hasValueRange ? std::max(std::fabs(m.minValue), std::fabs(m.maxValue))
                                              : std::max(std::fabs(axisMetric.range.min), std::fabs(axisMetric.range.max));
                        return ok && extent > 0.0 ? std::clamp(v / extent, -1.0, 1.0) : 0.0;
                    };
                    auto offset = [&](double when)
                    {
                        double x = axis(node.axes.at(i).first, when) * (m.invertX ? -1.0 : 1.0);
                        double y = -axis(node.axes.at(i).second, when) * (m.invertY ? -1.0 : 1.0);
                        const double length = std::hypot(x, y);
                        if (length > 1.0)
                            {
                                x /= length;
                                y /= length;
                            }
                        return QPointF(x * m.maxRadius, y * m.maxRadius);
                    };
                    if (m.trailSeconds > 0.0)
                        {
                            // a drawing in its own colors leaves an accent trail
                            const QColor color = component.color.role == GaugeColor::Original ? m_theme.accent : node.color;
                            drawTrail(painter, component.frame.center(), color, component.frame.width() * 0.3,
                                      m.trailSeconds, time, offset);
                        }
                    painter.translate(offset(time));
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
        case GaugeComponent::Gears:
            drawGears(painter, node, time);
            break;
        case GaugeComponent::LapTable:
            drawLapTable(painter, node, time);
            break;
        case GaugeComponent::SectorBar:
            drawSectorBar(painter, node, time);
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
    if (node.component.type != GaugeComponent::Graph && node.component.tiles)
        {
            drawTiles(painter, node, route);
        }
    painter.setBrush(Qt::NoBrush);
    // bevel joins: the same on a line this thin, and round ones cost an arc at every corner
    painter.setPen(QPen(node.color, node.component.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::BevelJoin));
    painter.drawPath(node.component.type == GaugeComponent::Graph ? node.line : node.routes.at(route).path);
}

void BoundGauge::drawTiles(QPainter &painter, const Node &node, int route) const
{
    const QImage &map = node.routes.at(route).map;
    if (map.isNull())
        {
            return;
        }
    const QRectF frame = node.component.frame;
    painter.save();
    QPainterPath shape;
    shape.addRoundedRect(frame, node.component.cornerRadius, node.component.cornerRadius);
    painter.setClipPath(shape, Qt::IntersectClip);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(painter.opacity() * node.component.tileOpacity);
    painter.drawImage(node.routes.at(route).mapRect, map);
    painter.restore();
    drawCredit(painter, node);
}

void BoundGauge::drawFollowing(QPainter &painter, const Node &node, const Route &route, double time, const Context &context) const
{
    const QRectF frame = node.component.frame;
    // where the rider is goes to the middle; before there is a position, the start does
    QPointF position;
    const QPointF here = positionAt(time, &position) ? route.toFrame.map(position) : route.startPoint;
    const QPointF shift = frame.center() - here;
    const QSizeF size = followSize(node.component);
    const QRectF shown(here.x() - size.width() / 2.0, here.y() - size.height() / 2.0, size.width(), size.height());
    bool headingKnown = false;
    const double heading = node.component.headingUp && m_heading ? m_heading->valueAt(time, &headingKnown) : 0.0;

    painter.save();
    QPainterPath shape;
    shape.addRoundedRect(frame, node.component.cornerRadius, node.component.cornerRadius);
    painter.setClipPath(shape, Qt::IntersectClip);
    if (headingKnown)
        {
            // the way ahead up: everything turns back by the heading, about the rider
            painter.translate(frame.center());
            painter.rotate(-heading);
            painter.translate(-frame.center());
        }
    painter.translate(shift);
    bool streets = false;
    if (node.component.tiles)
        {
            const TileStore::Picture map = TileStore::instance()->picture(route.toFrame.inverted().mapRect(shown), followPixels(node.component));
            if (!map.image.isNull())
                {
                    painter.save();
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    painter.setOpacity(painter.opacity() * node.component.tileOpacity);
                    painter.drawImage(route.toFrame.mapRect(map.bounds), map.image);
                    painter.restore();
                    streets = true;
                }
        }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(node.color, node.component.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double edge = node.component.lineWidth;
    const QRectF reach = shown.adjusted(-edge, -edge, edge, edge);
    for (int run = 0; run < route.runBounds.size(); ++run)
        {
            // a run straight north or east has bounds of no width: intersects() would miss it
            const QRectF &bounds = route.runBounds.at(run);
            if (bounds.right() >= reach.left() && bounds.left() <= reach.right()
                    && bounds.bottom() >= reach.top() && bounds.top() <= reach.bottom())
                {
                    const int from = run * kRunLength;
                    painter.drawPolyline(route.points.constData() + from,
                                         std::min(kRunLength + 1, int(route.points.size()) - from));
                }
        }
    drawAt(painter, node.startLine, route.startPoint, time, context, route.startAngle);
    // (back at the frame, still turned)
    painter.translate(-shift);
    drawAt(painter, node.knobTurning, frame.center(), time, context);
    painter.restore();
    if (streets)
        {
            drawCredit(painter, node);
        }
    drawAt(painter, node.knob, frame.center(), time, context);
}

void BoundGauge::drawCredit(QPainter &painter, const Node &node) const
{
    const QRectF frame = node.component.frame;
    // the credit the map's makers ask for, small in the corner
    const QString credit = TileStore::instance()->attribution();
    if (credit.isEmpty())
        {
            return;
        }
    painter.save();
    QFont font(GaugeFonts::Family);
    font.setPixelSize(std::max(7, int(std::lround(frame.height() * 0.034))));
    const double inset = std::max(node.component.cornerRadius * 0.6, font.pixelSize() * 0.4);
    // a long credit is set smaller, not cut
    const double room = frame.width() - 2 * inset - font.pixelSize() * 0.8;
    const double wanted = QFontMetricsF(font).horizontalAdvance(credit);
    if (wanted > room && room > 0.0)
        {
            font.setPixelSize(std::max(5, int(std::floor(font.pixelSize() * room / wanted))));
        }
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double margin = font.pixelSize() * 0.4;
    QRectF box(0, 0, metrics.horizontalAdvance(credit) + 2 * margin, metrics.height());
    box.moveBottomRight(QPointF(frame.right() - inset, frame.bottom() - margin));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 170));
    painter.drawRoundedRect(box, margin, margin);
    painter.setPen(QColor(30, 30, 30));
    painter.drawText(box, Qt::AlignCenter, credit);
    painter.restore();
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
    if (route.follows)
        {
            drawFollowing(painter, node, route, time, context);
            return;
        }
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
    else if (!metric.clock.isEmpty())
        {
            // bound to the time of day or the date: the recording's clock, where the recording was made
            GaugeComponent clock = component;
            clock.clockFormat = metric.clock;
            clock.localTime = true;
            number = clockText(clock, time);
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
                    number = durationText(value, decimals, sign);
                    break;
                case GaugeComponent::DegreesMinutesSeconds:
                    number = degreesText(value, decimals);
                    break;
                default:
                    // a pace reads as minutes and whole seconds, whatever places the gauge was made for
                    number = metric.display.minutesSeconds ? durationText(value, 0, sign)
                             : sign + QString::number(value, 'f', decimals);
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
            text = withUnit(number, metric.display.unit);
            break;
        case GaugeComponent::Label:
            text = metric.display.name;
            break;
        case GaugeComponent::LabelValueUnit:
            text = metric.display.name + " " + withUnit(number, metric.display.unit);
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

    painter.setFont(fontOf(painter, component, m_theme));
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
// 4K export too. With a color of its own it is one shape in that color, as
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
    painter.drawImage(frame, tintedSvg(renderer, node.component.svg, size, node.color));
}

namespace
{
// The teeth of a gear channel at `time`, those before its last change, and when that was;
// 0 teeth where the channel says nothing.
struct Shift
{
    int now{0};
    int before{0};
    double since{-1e9};
};

// The place of the ring or sprocket nearest to `teeth` (recorded gears may not match the teeth
// entered for the bike); -1 for none.
int nearest(const QVector<int> &all, int teeth)
{
    if (teeth <= 0 || all.isEmpty())
        {
            return -1;
        }
    int best = 0;
    for (int i = 1; i < all.size(); ++i)
        {
            if (std::abs(all.at(i) - teeth) < std::abs(all.at(best) - teeth))
                {
                    best = i;
                }
        }
    return best;
}

Shift shiftAt(const Channel *channel, double time)
{
    Shift shift;
    if (!channel || channel->isEmpty() || time < channel->startTime())
        {
            return shift;
        }
    int i = 0;
    int high = channel->count() - 1;
    // the last sample not after `time`
    while (i < high)
        {
            const int middle = (i + high + 1) / 2;
            channel->timeAt(middle) <= time ? i = middle : high = middle - 1;
        }
    shift.now = int(channel->valueAtIndex(i));
    // back to where this gear began
    while (i > 0 && int(channel->valueAtIndex(i - 1)) == shift.now)
        {
            --i;
        }
    shift.since = channel->timeAt(i);
    shift.before = i > 0 ? int(channel->valueAtIndex(i - 1)) : shift.now;
    return shift;
}
}

void BoundGauge::drawGears(QPainter &painter, const Node &node, double time) const
{
    const QVector<int> rings = m_session ? m_session->chainrings() : QVector<int>();
    const QVector<int> sprockets = m_session ? m_session->cassette() : QVector<int>();
    if (sprockets.isEmpty())
        {
            return;
        }
    const GaugeComponent &component = node.component;
    const QRectF frame = component.frame;
    int largest = 1;
    for (const int teeth : rings + sprockets)
        {
            largest = std::max(largest, teeth);
        }
    // the rings, a gap, then the sprockets from the largest (gear 1) to the smallest
    const double places = rings.size() + sprockets.size() + (rings.isEmpty() ? 0.0 : 1.0);
    const double slot = frame.width() / std::max(1.0, places);
    const double barWidth = std::max(1.0, slot * 0.42);
    const auto heightOf = [&](int teeth) { return frame.height() * teeth / largest; };
    const auto ringX = [&](int index) { return frame.left() + slot * (index + 0.5); };
    const auto sprocketX = [&](int index)   // index into `sprockets`, smallest first
    {
        const int fromLargest = int(sprockets.size()) - 1 - index;
        return frame.left() + slot * ((rings.isEmpty() ? 0.0 : rings.size() + 1.0) + fromLargest + 0.5);
    };
    const Shift front = shiftAt(m_session->channel(ChannelId::FrontGear), time);
    const Shift rear = shiftAt(m_session->channel(ChannelId::RearGear), time);
    const int ringNow = rings.size() == 1 ? 0 : nearest(rings, front.now);
    const int sprocketNow = nearest(sprockets, rear.now);

    // the others faint, those in use in the active color
    QColor idle = node.color;
    idle.setAlphaF(idle.alphaF() * 0.45);
    painter.setPen(Qt::NoPen);
    const auto bar = [&](double x, int teeth, const QColor &color)
    {
        const double h = heightOf(teeth);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(x - barWidth / 2.0, frame.center().y() - h / 2.0, barWidth, h),
                                barWidth / 2.0, barWidth / 2.0);
    };
    for (int i = 0; i < rings.size(); ++i)
        {
            bar(ringX(i), rings.at(i), i == ringNow ? node.markerColor : idle);
        }
    for (int i = 0; i < sprockets.size(); ++i)
        {
            bar(sprocketX(i), sprockets.at(i), i == sprocketNow ? node.markerColor : idle);
        }
    if (ringNow < 0 || sprocketNow < 0 || rings.isEmpty())
        {
            return;
        }
    // the chain, sliding from the old gear to the new one for a moment after a shift
    const auto eased = [](double p) { return p * p * (3.0 - 2.0 * p); };
    const auto along = [&](const Shift &shift, int now, const auto &xOf, const QVector<int> &teeth, double *x, double *h)
    {
        const int before = nearest(teeth, shift.before);
        const double p = before < 0 ? 1.0 : eased(std::clamp((time - shift.since) / 0.35, 0.0, 1.0));
        const int from = before < 0 ? now : before;
        *x = xOf(from) + (xOf(now) - xOf(from)) * p;
        *h = heightOf(teeth.at(from)) + (heightOf(teeth.at(now)) - heightOf(teeth.at(from))) * p;
    };
    double ringAt = 0.0, ringHeight = 0.0, sprocketAt = 0.0, sprocketHeight = 0.0;
    along(front, ringNow, ringX, rings, &ringAt, &ringHeight);
    along(rear, sprocketNow, sprocketX, sprockets, &sprocketAt, &sprocketHeight);
    QColor chain = node.markerColor;
    chain.setAlphaF(chain.alphaF() * 0.8);
    painter.setPen(QPen(chain, component.lineWidth, Qt::SolidLine, Qt::RoundCap));
    const double middle = frame.center().y();
    painter.drawLine(QPointF(ringAt, middle - ringHeight / 2.0), QPointF(sprocketAt, middle - sprocketHeight / 2.0));
    painter.drawLine(QPointF(ringAt, middle + ringHeight / 2.0), QPointF(sprocketAt, middle + sprocketHeight / 2.0));
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
    const QPointF center = c.frame.center();
    if (s.arcWidth > 0.0 && setPen(painter, node.arcColor, s.arcWidth))
        {
            const QRectF circle(center.x() - s.radius, center.y() - s.radius, 2.0 * s.radius, 2.0 * s.radius);
            QPainterPath arc;
            arc.arcMoveTo(circle, qtAngle(s.startAngle));
            arc.arcTo(circle, qtAngle(s.startAngle), -(s.endAngle - s.startAngle));
            painter.drawPath(arc);
        }
    // ticks reach in from the arc's inner edge
    const double edge = s.radius - s.arcWidth / 2.0;
    painter.setFont(fontOf(painter, c, m_theme));
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
                            painter.drawLine(center + direction * (edge - ticks->length), center + direction * edge);
                        }
                    if (ticks == &s.major && node.color.alpha() > 0)
                        {
                            painter.setPen(node.color);
                            drawAnchored(painter, scaleNumber(value), center + direction * (s.radius - s.labelOffset), c.alignment);
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
    painter.setFont(fontOf(painter, c, m_theme));
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

void BoundGauge::prepareLaps()
{
    if (!m_session)
        {
            return;
        }
    QVector<TelemetrySession::Lap> laps = m_session->laps();
    std::sort(laps.begin(), laps.end(), [](const TelemetrySession::Lap &a, const TelemetrySession::Lap &b)
    {
        return a.startTime < b.startTime;
    });
    for (const TelemetrySession::Lap &lap : std::as_const(laps))
        {
            if (lap.endTime > lap.startTime)
                {
                    m_laps.append({int(m_laps.size()) + 1, lap.startTime, lap.endTime, true, lap.splits});
                }
        }
    // the lap under way at the end has a number but is not among the finished ones
    const Channel *number = m_session->channel(ChannelId::Lap);
    if (number && !number->isEmpty() && int(number->valueAtIndex(number->count() - 1)) == m_laps.size() + 1)
        {
            LapSpan open{int(m_laps.size()) + 1, number->timeAt(number->count() - 1), m_session->duration(), false, {}};
            // its sectors so far: where the sector number went up
            if (const Channel *sector = m_session->channel(ChannelId::Sector))
                {
                    for (int i = 0; i < sector->count(); ++i)
                        {
                            if (sector->timeAt(i) > open.start && sector->valueAtIndex(i) > 1.0)
                                {
                                    open.splits.append(sector->timeAt(i));
                                }
                        }
                }
            if (open.end > open.start)
                {
                    m_laps.append(open);
                }
        }
    for (const LapSpan &lap : std::as_const(m_laps))
        {
            if (lap.finished && !lap.splits.isEmpty())
                {
                    m_sectors = std::max(m_sectors, int(lap.splits.size()) + 1);
                }
        }
}

void BoundGauge::drawLapTable(QPainter &painter, const Node &node, double time) const
{
    const GaugeComponent &component = node.component;
    // the laps there are by `time`: those finished, the fastest of them, and the one under way
    QVector<int> shown;
    int best = -1;
    for (int i = 0; i < m_laps.size() && m_laps.at(i).start <= time; ++i)
        {
            const LapSpan &lap = m_laps.at(i);
            const bool done = lap.finished && lap.end <= time;
            if (done && (best < 0 || lap.end - lap.start < m_laps.at(best).end - m_laps.at(best).start))
                {
                    best = i;
                }
            if (done || time < lap.end)
                {
                    shown.append(i);
                }
        }
    const int rows = std::max(1, component.rows);
    if (shown.size() > rows)
        {
            shown.remove(0, shown.size() - rows);
        }
    const int decimals = component.precision >= 0 ? component.precision : 2;
    const QRectF frame = component.frame;
    const double rowHeight = frame.height() / rows;
    painter.setFont(fontOf(painter, component, m_theme));
    for (int row = 0; row < shown.size(); ++row)
        {
            const LapSpan &lap = m_laps.at(shown.at(row));
            const bool done = lap.finished && lap.end <= time;
            const double duration = (done ? lap.end : time) - lap.start;
            const double middle = frame.top() + rowHeight * (row + 0.5);
            painter.setPen(shown.at(row) == best ? node.markerColor : node.color);
            drawAnchored(painter, QString::number(lap.number), QPointF(frame.left(), middle), Qt::AlignLeft | Qt::AlignVCenter);
            drawAnchored(painter, durationText(duration, done ? decimals : std::min(decimals, 1)),
                         QPointF(frame.left() + frame.width() * 0.62, middle), Qt::AlignRight | Qt::AlignVCenter);
            if (done && best >= 0 && shown.at(row) != best)
                {
                    const double behind = duration - (m_laps.at(best).end - m_laps.at(best).start);
                    drawAnchored(painter, '+' + QString::number(behind, 'f', decimals), QPointF(frame.right(), middle),
                                 Qt::AlignRight | Qt::AlignVCenter);
                }
        }
}

void BoundGauge::drawSectorBar(QPainter &painter, const Node &node, double time) const
{
    const GaugeComponent &component = node.component;
    // without sector lines the bar waits, empty, for them
    const int count = m_sectors > 0 ? m_sectors : 3;
    const QRectF frame = component.frame;
    const double gap = std::clamp(component.padding, 0.0, frame.width() / count);
    const double width = (frame.width() - gap * (count - 1)) / count;
    const auto segment = [&](int k, double fraction)
    {
        return QRectF(frame.left() + k * (width + gap), frame.top(), width * fraction, frame.height());
    };
    QColor idle = node.color;
    idle.setAlphaF(idle.alphaF() * 0.25);
    painter.setPen(Qt::NoPen);
    const double corner = std::min(component.cornerRadius, std::min(width, frame.height()) / 2.0);
    const auto fill = [&](const QRectF &rect, const QColor &color)
    {
        painter.setBrush(color);
        painter.drawRoundedRect(rect, corner, corner);
    };
    for (int k = 0; k < count; ++k)
        {
            fill(segment(k, 1.0), idle);
        }
    // the lap under way at `time`, when it has its sectors
    int current = -1;
    for (int i = 0; i < m_laps.size(); ++i)
        {
            if (m_laps.at(i).start <= time && time < m_laps.at(i).end)
                {
                    current = i;
                }
        }
    if (current < 0 || m_sectors == 0 || (m_laps.at(current).finished && m_laps.at(current).splits.isEmpty()))
        {
            return;
        }
    const LapSpan &lap = m_laps.at(current);
    const QVector<double> bounds = QVector<double>{lap.start} + lap.splits + QVector<double>{lap.end};
    // the best of sector k in the finished laps before this one
    const auto bestBefore = [&](int k)
    {
        double best = std::numeric_limits<double>::max();
        for (int i = 0; i < current; ++i)
            {
                const LapSpan &earlier = m_laps.at(i);
                if (earlier.splits.size() + 1 == m_sectors)
                    {
                        const QVector<double> b = QVector<double>{earlier.start} + earlier.splits + QVector<double>{earlier.end};
                        best = std::min(best, b.at(k + 1) - b.at(k));
                    }
            }
        return best;
    };
    for (int k = 0; k < count && k + 1 < bounds.size(); ++k)
        {
            const double best = bestBefore(k);
            if (bounds.at(k + 1) <= time)
                {
                    fill(segment(k, 1.0), bounds.at(k + 1) - bounds.at(k) <= best ? node.markerColor : node.color);
                    continue;
                }
            // the sector being driven, against the best of it; the first time round it stays empty
            if (best < std::numeric_limits<double>::max())
                {
                    QColor running = node.color;
                    running.setAlphaF(running.alphaF() * 0.5);
                    fill(segment(k, std::clamp((time - bounds.at(k)) / best, 0.0, 1.0)), running);
                }
            break;
        }
}
