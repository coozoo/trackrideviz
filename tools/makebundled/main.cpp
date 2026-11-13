// Draws the gauges that ship with the program and writes them, with the
// templates built from them, into the data folder (see makebundled.pro).
//
// Every picture is a white shape on transparent: the theme colours it. Only
// artwork drawn here, by this program, may go into data/.

#include "gauge/gaugefile.h"
#include "gauge/gaugelibrary.h"
#include "overlay/gaugetemplate.h"
#include "settings/bundleddata.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"

#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <cstdio>

namespace
{
const double kPi = 3.14159265358979323846;

QPointF onCircle(const QPointF &centre, double angle, double radius)
{
    const double rad = angle * kPi / 180.0;
    return QPointF(centre.x() + radius * std::sin(rad), centre.y() - radius * std::cos(rad));
}

// A picture of `size` design units, drawn by `paint` in white.
template<typename Paint>
QImage picture(const QSizeF &size, Paint paint)
{
    QImage image(size.toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    paint(painter);
    return image;
}

GaugeComponent image(const QImage &img, const QRectF &frame, GaugeColor::Role role, const QString &file)
{
    GaugeComponent c;
    c.type = GaugeComponent::Image;
    c.image = img;
    c.imagePath = "images/" + file + ".png";
    c.frame = frame;
    c.color.role = role;
    return c;
}

GaugeComponent text(GaugeComponent::AutoText autoText, const QPointF &anchor, double fontSize, Qt::Alignment alignment,
                    GaugeColor::Role role = GaugeColor::Foreground, bool bold = false)
{
    GaugeComponent c;
    c.type = GaugeComponent::Text;
    c.autoText = autoText;
    c.frame = QRectF(anchor, QSizeF(0, 0));
    c.fontSize = fontSize;
    c.alignment = alignment;
    c.color.role = role;
    c.bold = bold;
    return c;
}

GaugeComponent label(const QPointF &anchor, double fontSize, Qt::Alignment alignment)
{
    GaugeComponent c = text(GaugeComponent::Label, anchor, fontSize, alignment, GaugeColor::Accent2);
    c.upperCase = true;
    return c;
}

// A plate with rounded corners filling the whole gauge.
GaugeComponent plate(const QSizeF &size, double radius)
{
    const QImage img = picture(size, [&](QPainter &p)
    {
        p.drawRoundedRect(QRectF(QPointF(0, 0), size), radius, radius);
    });
    return image(img, QRectF(QPointF(0, 0), size), GaugeColor::Background, "plate");
}

// A dot centred on its origin, for knobs: a light ring around a coloured middle.
QVector<GaugeComponent> dot(double radius)
{
    const QSizeF size(2 * radius + 4, 2 * radius + 4);
    const QRectF frame(-size.width() / 2, -size.height() / 2, size.width(), size.height());
    const QPointF middle(size.width() / 2, size.height() / 2);
    const QImage ring = picture(size, [&](QPainter &p)
    {
        p.drawEllipse(middle, radius + 1, radius + 1);
    });
    const QImage inner = picture(size, [&](QPainter &p)
    {
        p.drawEllipse(middle, radius * 0.62, radius * 0.62);
    });
    return {image(ring, frame, GaugeColor::Foreground, "knob-ring"), image(inner, frame, GaugeColor::Accent, "knob")};
}

// Speed and the like on an arc that fills up, with the value in the middle.
Gauge arc()
{
    const QSizeF size(400, 400);
    const QPointF centre(200, 200);
    const QRectF whole(QPointF(0, 0), size);
    const double from = -125.0;
    const double to = 125.0;
    const double radius = 158.0;
    Gauge gauge;
    gauge.name = "Arc";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::Speed, ChannelId::HeartRate, ChannelId::Cadence, ChannelId::Power};

    gauge.components << image(picture(size, [&](QPainter &p)
    {
        p.drawEllipse(centre, 196, 196);
    }), whole, GaugeColor::Background, "plate");

    auto band = [&](QPainter &p)
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 30, Qt::SolidLine, Qt::FlatCap));
        const QRectF circle(centre.x() - radius, centre.y() - radius, 2 * radius, 2 * radius);
        // QPainter counts angles counter-clockwise from 3 o'clock, in 1/16 degrees
        p.drawArc(circle, int((90.0 - from) * 16), int(-(to - from) * 16));
    };
    QImage track = picture(size, band);
    {
        // the empty part of the scale, faint
        QPainter p(&track);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(track.rect(), QColor(0, 0, 0, 60));
    }
    gauge.components << image(track, whole, GaugeColor::Accent2, "track");

    GaugeComponent fill = image(picture(size, band), whole, GaugeColor::Accent, "fill");
    GaugeManipulation sweep;
    sweep.type = GaugeManipulation::RadialClip;
    sweep.pivot = centre;
    sweep.minAngle = from;
    sweep.maxAngle = to;
    fill.manipulations << sweep;
    gauge.components << fill;

    const int marks = 6;
    gauge.components << image(picture(size, [&](QPainter &p)
    {
        for (int i = 0; i < marks; ++i)
            {
                const double angle = from + (to - from) * i / (marks - 1);
                p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::FlatCap));
                p.drawLine(onCircle(centre, angle, radius + 18), onCircle(centre, angle, radius + 30));
            }
    }), whole, GaugeColor::Accent2, "ticks");
    for (int i = 0; i < marks; ++i)
        {
            const double angle = from + (to - from) * i / (marks - 1);
            gauge.components << text(GaugeComponent::IntervalMarking, onCircle(centre, angle, radius - 38), 22,
                                     Qt::AlignCenter, GaugeColor::Foreground, true);
        }

    GaugeComponent value = text(GaugeComponent::Value, QPointF(200, 212), 92, Qt::AlignCenter, GaugeColor::Foreground, true);
    value.precision = 0;
    gauge.components << value;
    gauge.components << text(GaugeComponent::Unit, QPointF(200, 284), 26, Qt::AlignCenter, GaugeColor::Accent2);
    return gauge;
}

// A horizontal bar with slanted ends, label and value above it.
Gauge bar()
{
    const QSizeF size(600, 130);
    const QRectF whole(QPointF(0, 0), size);
    const QRectF band(24, 70, 552, 36);
    const double angle = 20.0;
    const double slant = band.height() * std::tan(angle * kPi / 180.0);
    Gauge gauge;
    gauge.name = "Bar";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::Speed, ChannelId::Cadence, ChannelId::Power, ChannelId::HeartRate};
    gauge.components << plate(size, 18);

    QPainterPath shape;
    shape.moveTo(band.left() + slant, band.top());
    shape.lineTo(band.right(), band.top());
    shape.lineTo(band.right() - slant, band.bottom());
    shape.lineTo(band.left(), band.bottom());
    shape.closeSubpath();
    const QImage full = picture(size, [&](QPainter &p)
    {
        p.drawPath(shape);
    });
    QImage track = full;
    {
        QPainter p(&track);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(track.rect(), QColor(0, 0, 0, 60));
    }
    gauge.components << image(track, whole, GaugeColor::Accent2, "track");
    GaugeComponent fill = image(full, whole, GaugeColor::Accent, "fill");
    GaugeManipulation clip;
    clip.type = GaugeManipulation::WidthClip;
    // the moving edge leans like the bar's ends
    clip.minX = band.left();
    clip.maxX = band.right();
    clip.clipAngle = angle;
    fill.manipulations << clip;
    gauge.components << fill;

    gauge.components << label(QPointF(24, 38), 24, Qt::AlignLeft | Qt::AlignVCenter);
    gauge.components << text(GaugeComponent::ValueAndUnit, QPointF(576, 38), 34, Qt::AlignRight | Qt::AlignVCenter,
                             GaugeColor::Foreground, true);
    return gauge;
}

// A column that rises with the value.
Gauge column()
{
    const QSizeF size(170, 460);
    const QRectF whole(QPointF(0, 0), size);
    const QRectF band(55, 112, 60, 290);
    Gauge gauge;
    gauge.name = "Column";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::Power, ChannelId::Grade, ChannelId::HeartRate, ChannelId::Cadence};
    gauge.components << plate(size, 18);

    const QImage full = picture(size, [&](QPainter &p)
    {
        p.drawRoundedRect(band, 8, 8);
    });
    QImage track = full;
    {
        QPainter p(&track);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(track.rect(), QColor(0, 0, 0, 60));
    }
    gauge.components << image(track, whole, GaugeColor::Accent2, "track");
    GaugeComponent fill = image(full, whole, GaugeColor::Accent, "fill");
    GaugeManipulation clip;
    clip.type = GaugeManipulation::HeightClip;
    clip.minY = band.bottom();
    clip.maxY = band.top();
    fill.manipulations << clip;
    gauge.components << fill;

    gauge.components << label(QPointF(85, 32), 20, Qt::AlignCenter);
    gauge.components << text(GaugeComponent::Value, QPointF(85, 74), 40, Qt::AlignCenter, GaugeColor::Foreground, true);
    gauge.components << text(GaugeComponent::Unit, QPointF(85, 428), 22, Qt::AlignCenter, GaugeColor::Accent2);
    return gauge;
}

// The channel over the whole ride, with the current point marked.
Gauge profile()
{
    const QSizeF size(700, 220);
    const QRectF area(24, 66, 652, 132);
    Gauge gauge;
    gauge.name = "Profile";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::Elevation, ChannelId::Speed, ChannelId::HeartRate, ChannelId::Power};
    gauge.components << plate(size, 18);
    gauge.components << label(QPointF(24, 36), 22, Qt::AlignLeft | Qt::AlignVCenter);
    gauge.components << text(GaugeComponent::ValueAndUnit, QPointF(676, 36), 30, Qt::AlignRight | Qt::AlignVCenter,
                             GaugeColor::Foreground, true);

    GaugeComponent graph;
    graph.type = GaugeComponent::Graph;
    graph.frame = area;
    graph.color.role = GaugeColor::Accent;
    graph.lineWidth = 3;
    graph.fillAlpha = 80;
    graph.padding = 4;
    // a thin line down from the top at the current time
    const QSizeF lineSize(4, area.height());
    graph.xMarker << image(picture(lineSize, [&](QPainter &p)
    {
        p.fillRect(QRectF(1, 0, 2, lineSize.height()), QColor(255, 255, 255, 140));
    }), QRectF(-2, 0, lineSize.width(), lineSize.height()), GaugeColor::Foreground, "marker");
    graph.knob = dot(9);
    gauge.components << graph;
    return gauge;
}

// The whole route, or the lap under way, with the current position.
Gauge map(bool lap)
{
    const QSizeF size(420, 420);
    Gauge gauge;
    gauge.name = lap ? "Lap map" : "Route map";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::Speed};
    gauge.components << plate(size, 24);

    GaugeComponent route;
    route.type = lap ? GaugeComponent::LapMap : GaugeComponent::TrackMap;
    route.frame = QRectF(24, 24, 372, 372);
    route.padding = 12;
    route.lineWidth = 6;
    route.color.role = GaugeColor::Foreground;
    route.knob = dot(13);
    if (lap)
        {
            // drawn across the direction of travel where the lap began
            const QSizeF lineSize(40, 10);
            route.startLine << image(picture(lineSize, [&](QPainter &p)
            {
                p.drawRoundedRect(QRectF(QPointF(0, 0), lineSize), 3, 3);
            }), QRectF(-lineSize.width() / 2, -lineSize.height() / 2, lineSize.width(), lineSize.height()),
            GaugeColor::Accent2, "start");
        }
    gauge.components << route;
    return gauge;
}

// A picture of what is measured beside label, value and unit.
Gauge iconReadout()
{
    const QSizeF size(360, 120);
    Gauge gauge;
    gauge.name = "Icon readout";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::HeartRate, ChannelId::Elevation, ChannelId::Cadence,
                                       ChannelId::Power, ChannelId::Temperature, ChannelId::Speed};
    gauge.components << plate(size, 18);
    GaugeComponent icon;
    icon.type = GaugeComponent::Image;
    icon.channelIcon = true;
    icon.frame = QRectF(18, 18, 84, 84);
    icon.color.role = GaugeColor::Accent;
    gauge.components << icon;
    gauge.components << label(QPointF(120, 36), 22, Qt::AlignLeft | Qt::AlignVCenter);
    gauge.components << text(GaugeComponent::ValueAndUnit, QPointF(120, 80), 40, Qt::AlignLeft | Qt::AlignVCenter,
                             GaugeColor::Foreground, true);
    return gauge;
}

// A ball pushed around by the sideways and lengthways forces.
Gauge gForce()
{
    const QSizeF size(400, 400);
    const QPointF centre(200, 200);
    const QRectF whole(QPointF(0, 0), size);
    Gauge gauge;
    gauge.name = "G-force";
    gauge.size = size;
    gauge.supportedTypes = QStringList{ChannelId::GForceLateral, ChannelId::GForceLongitudinal};
    gauge.components << image(picture(size, [&](QPainter &p)
    {
        p.drawEllipse(centre, 196, 196);
    }), whole, GaugeColor::Background, "plate");
    gauge.components << image(picture(size, [&](QPainter &p)
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 110), 2));
        for (double r : {58.0, 116.0, 174.0})
            {
                p.drawEllipse(centre, r, r);
            }
        p.drawLine(QPointF(26, 200), QPointF(374, 200));
        p.drawLine(QPointF(200, 26), QPointF(200, 374));
    }), whole, GaugeColor::Accent2, "rings");

    const double radius = 20;
    GaugeComponent ball = image(picture(QSizeF(2 * radius + 4, 2 * radius + 4), [&](QPainter &p)
    {
        p.drawEllipse(QPointF(radius + 2, radius + 2), radius, radius);
    }), QRectF(centre.x() - radius - 2, centre.y() - radius - 2, 2 * radius + 4, 2 * radius + 4), GaugeColor::Accent, "ball");
    GaugeManipulation move;
    move.type = GaugeManipulation::Translate2D;
    move.maxRadius = 174 - radius;
    move.xChannel = ChannelId::GForceLateral;
    move.yChannel = ChannelId::GForceLongitudinal;
    ball.manipulations << move;
    gauge.components << ball;
    return gauge;
}

struct Saved
{
    QString id;
    QSizeF size;
};

bool save(const QString &folder, const QString &name, const Gauge &gauge, QHash<QString, Saved> *saved)
{
    QString error;
    const QString path = folder + "/gauges/" + name;
    QDir(path).removeRecursively();
    if (!GaugeFile::save(path, gauge, &error))
        {
            std::fprintf(stderr, "%s: %s\n", qPrintable(name), qPrintable(error));
            return false;
        }
    saved->insert(name, {"bundled:" + name, gauge.size});
    return true;
}

// Places gauges by the corner they keep their distance to.
class Layout
{
public:
    explicit Layout(const QHash<QString, Saved> &gauges) : m_gauges(gauges) {}

    // `marginX` / `marginY`: reference pixels between the gauge and its edge
    // (from the middle for Middle: + right / down).
    TemplateItem place(const QString &gauge, const QString &channel, TemplateItem::Edge edgeX, TemplateItem::Edge edgeY,
                       double marginX, double marginY, double height) const
    {
        const Saved saved = m_gauges.value(gauge);
        const double width = height * saved.size.width() / saved.size.height();
        TemplateItem item;
        item.gaugeId = saved.id;
        item.channel = channel;
        item.edgeX = edgeX;
        item.edgeY = edgeY;
        item.offsetX = offset(edgeX, marginX, width);
        item.offsetY = offset(edgeY, marginY, height);
        item.height = height;
        return item;
    }

private:
    static double offset(TemplateItem::Edge edge, double margin, double extent)
    {
        switch (edge)
            {
            case TemplateItem::Start:
                return margin;
            case TemplateItem::Middle:
                return margin - extent / 2;
            case TemplateItem::End:
                break;
            }
        return -margin - extent;
    }
    QHash<QString, Saved> m_gauges;
};

bool save(const QString &folder, const QString &file, const GaugeTemplate &t)
{
    QString error;
    if (!TemplateFile::save(folder + "/templates/" + file + ".json", t, &error))
        {
            std::fprintf(stderr, "%s: %s\n", qPrintable(file), qPrintable(error));
            return false;
        }
    return true;
}

// Each template over a grey frame, with the made-up ride, as <name>.png.
void preview(const QString &folder, const QVector<GaugeTemplate> &templates, const QString &target)
{
    BundledData::setFolder(folder);
    GaugeLibrary::refresh();
    QDir().mkpath(target);
    const TelemetrySessionPtr session = DemoSession::create();
    for (const GaugeTemplate &t : templates)
        {
            QImage frame(1920, 1080, QImage::Format_ARGB32_Premultiplied);
            frame.fill(QColor(110, 120, 115));
            QPainter painter(&frame);
            painter.setRenderHint(QPainter::Antialiasing);
            const Overlay overlay(t.overlayItems(16.0 / 9.0), session, Units(), t.theme);
            overlay.render(painter, QRectF(frame.rect()), DemoSession::ShowcaseTime);
            painter.end();
            frame.save(target + "/" + t.name + ".png");
        }
}

GaugeTheme theme(const QColor &background, const QColor &accent, const QColor &accent2,
                 const QColor &foreground = QColor(255, 255, 255))
{
    GaugeTheme t;
    t.background = background;
    t.accent = accent;
    t.accent2 = accent2;
    t.foreground = foreground;
    return t;
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // makebundled [data folder] [--preview picture folder]
    QStringList args = app.arguments().mid(1);
    QString previewFolder;
    const int previewAt = int(args.indexOf("--preview"));
    if (previewAt >= 0 && previewAt + 1 < args.size())
        {
            previewFolder = args.at(previewAt + 1);
            args.remove(previewAt, 2);
        }
    const QString folder = !args.isEmpty() ? args.first() : QStringLiteral(RTV_DATA_FOLDER);
    // only what is drawn here: the drawn (SVG) gauges in data/gauges are made by hand
    QDir(folder + "/templates").removeRecursively();
    if (!QDir().mkpath(folder + "/templates"))
        {
            std::fprintf(stderr, "cannot create %s\n", qPrintable(folder));
            return 1;
        }

    QHash<QString, Saved> gauges;
    bool ok = save(folder, "Arc", arc(), &gauges)
              && save(folder, "Bar", bar(), &gauges)
              && save(folder, "Column", column(), &gauges)
              && save(folder, "Profile", profile(), &gauges)
              && save(folder, "Route map", map(false), &gauges)
              && save(folder, "Lap map", map(true), &gauges)
              && save(folder, "Icon readout", iconReadout(), &gauges)
              && save(folder, "G-force", gForce(), &gauges);
    if (!ok)
        {
            return 1;
        }

    using E = TemplateItem;
    const Layout at(gauges);

    GaugeTemplate ride;
    ride.name = "Ride";
    ride.category = "Cycling";
    ride.theme = theme(QColor(10, 14, 20, 150), QColor(255, 120, 30), QColor(220, 230, 240));
    ride.items << at.place("Arc", ChannelId::Speed, E::Start, E::End, 40, 40, 300)
               << at.place("Icon readout", ChannelId::HeartRate, E::Start, E::End, 360, 240, 100)
               << at.place("Bar", ChannelId::Cadence, E::Start, E::End, 360, 40, 100)
               << at.place("Profile", ChannelId::Elevation, E::End, E::End, 40, 40, 170)
               << at.place("Route map", ChannelId::Speed, E::End, E::Start, 40, 40, 300);
    ok = save(folder, "Ride", ride);

    GaugeTemplate track;
    track.name = "Track day";
    track.category = "Motorsport";
    track.theme = theme(QColor(0, 0, 0, 160), QColor(230, 30, 40), QColor(255, 255, 255));
    track.items << at.place("Arc", ChannelId::Speed, E::Start, E::End, 40, 40, 340)
                << at.place("Lap map", ChannelId::Speed, E::End, E::Start, 40, 40, 340)
                << at.place("G-force", ChannelId::GForceLateral, E::End, E::End, 40, 40, 260)
                << at.place("Bar", ChannelId::Speed, E::Middle, E::Start, 0, 40, 90);
    ok = ok && save(folder, "Track day", track);

    GaugeTemplate hike;
    hike.name = "Climb";
    hike.category = "Outdoor";
    hike.theme = theme(QColor(20, 40, 30, 150), QColor(120, 220, 90), QColor(230, 240, 220));
    hike.items << at.place("Profile", ChannelId::Elevation, E::Middle, E::End, 0, 40, 200)
               << at.place("Column", ChannelId::Grade, E::Start, E::End, 40, 40, 330)
               << at.place("Icon readout", ChannelId::Speed, E::Start, E::Start, 40, 40, 100)
               << at.place("Icon readout", ChannelId::HeartRate, E::Start, E::Start, 40, 160, 100)
               << at.place("Route map", ChannelId::Speed, E::End, E::Start, 40, 40, 280);
    ok = ok && save(folder, "Climb", hike);

    GaugeTemplate minimal;
    minimal.name = "Minimal";
    minimal.category = "Simple";
    minimal.theme = theme(QColor(0, 0, 0, 110), QColor(255, 255, 255), QColor(200, 200, 200));
    minimal.items << at.place("Icon readout", ChannelId::Speed, E::Start, E::End, 40, 40, 90)
                  << at.place("Icon readout", ChannelId::HeartRate, E::Start, E::End, 40, 140, 90);
    ok = ok && save(folder, "Minimal", minimal);

    if (!previewFolder.isEmpty())
        {
            preview(folder, {ride, track, hike, minimal}, previewFolder);
        }
    std::printf("%d gauges and 4 templates written to %s\n", int(gauges.size()), qPrintable(folder));
    return ok ? 0 : 1;
}
