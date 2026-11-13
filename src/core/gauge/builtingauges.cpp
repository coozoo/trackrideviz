#include "builtingauges.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace
{
const double kPi = 3.14159265358979323846;

QImage blankImage(const QSizeF &size)
{
    QImage image(size.toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

GaugeComponent imageComponent(const QImage &image, const QRectF &frame, GaugeColor::Role role)
{
    GaugeComponent component;
    component.type = GaugeComponent::Image;
    component.image = image;
    component.frame = frame;
    component.color.role = role;
    return component;
}

GaugeComponent textComponent(GaugeComponent::AutoText autoText, const QPointF &anchor, double fontSize,
                             Qt::Alignment alignment, GaugeColor::Role role = GaugeColor::Foreground)
{
    GaugeComponent component;
    component.type = GaugeComponent::Text;
    component.autoText = autoText;
    component.frame = QRectF(anchor, QSizeF(0, 0));
    component.fontSize = fontSize;
    component.alignment = alignment;
    component.color.role = role;
    return component;
}
}

QStringList BuiltinGauges::ids()
{
    return {DialId, ReadoutId};
}

Gauge BuiltinGauges::byId(const QString &id)
{
    if (id == QLatin1String(DialId))
        {
            return dial();
        }
    if (id == QLatin1String(ReadoutId))
        {
            return readout();
        }
    return Gauge();
}

// Every picture here is a white shape; the theme gives it its colour.
Gauge BuiltinGauges::dial()
{
    const double size = 400.0;
    const double centre = size / 2;
    // the scale runs clockwise from lower left to lower right
    const double minAngle = -135.0;
    const double maxAngle = 135.0;
    const int majorTicks = 9;
    const QRectF whole(0, 0, size, size);
    auto onDial = [centre](double angle, double radius)
    {
        const double rad = angle * kPi / 180.0;
        return QPointF(centre + radius * std::sin(rad), centre - radius * std::cos(rad));
    };

    Gauge gauge;
    gauge.name = "Dial basic";
    gauge.size = QSizeF(size, size);

    QImage plate = blankImage(gauge.size);
    {
        QPainter painter(&plate);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QPointF(centre, centre), centre - 2, centre - 2);
    }
    gauge.components.append(imageComponent(plate, whole, GaugeColor::Background));

    QImage ticks = blankImage(gauge.size);
    {
        QPainter painter(&ticks);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 4));
        painter.drawEllipse(QPointF(centre, centre), centre - 4, centre - 4);
        const int steps = (majorTicks - 1) * 2;
        for (int i = 0; i <= steps; ++i)
            {
                const double angle = minAngle + (maxAngle - minAngle) * i / steps;
                const bool major = i % 2 == 0;
                painter.setPen(QPen(Qt::white, major ? 5 : 2, Qt::SolidLine, Qt::FlatCap));
                painter.drawLine(onDial(angle, major ? 158 : 170), onDial(angle, 186));
            }
    }
    gauge.components.append(imageComponent(ticks, whole, GaugeColor::Accent2));

    for (int i = 0; i < majorTicks; ++i)
        {
            const double angle = minAngle + (maxAngle - minAngle) * i / (majorTicks - 1);
            GaugeComponent marking = textComponent(GaugeComponent::IntervalMarking, onDial(angle, 128), 26,
                                                   Qt::AlignHCenter | Qt::AlignVCenter);
            marking.bold = true;
            gauge.components.append(marking);
        }

    GaugeComponent value = textComponent(GaugeComponent::Value, QPointF(centre, 297), 64, Qt::AlignHCenter | Qt::AlignVCenter);
    value.bold = true;
    value.precision = 0;
    gauge.components.append(value);
    gauge.components.append(textComponent(GaugeComponent::Unit, QPointF(centre, 343), 24, Qt::AlignHCenter | Qt::AlignVCenter));

    // drawn pointing at 12 o'clock; the manipulation turns it around the centre
    QImage needle = blankImage(gauge.size);
    {
        QPainter painter(&needle);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.moveTo(centre, centre - 176);
        path.lineTo(centre + 7, centre + 30);
        path.lineTo(centre - 7, centre + 30);
        path.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPath(path);
        painter.drawEllipse(QPointF(centre, centre), 16, 16);
    }
    GaugeComponent hand = imageComponent(needle, whole, GaugeColor::Accent);
    GaugeManipulation turn;
    turn.type = GaugeManipulation::Rotate;
    turn.pivot = QPointF(centre, centre);
    turn.minAngle = minAngle;
    turn.maxAngle = maxAngle;
    hand.manipulations.append(turn);
    gauge.components.append(hand);
    return gauge;
}

Gauge BuiltinGauges::readout()
{
    Gauge gauge;
    gauge.name = "Readout";
    gauge.size = QSizeF(320, 110);

    QImage plate = blankImage(gauge.size);
    {
        QPainter painter(&plate);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(QPointF(0, 0), gauge.size), 16, 16);
    }
    gauge.components.append(imageComponent(plate, QRectF(QPointF(0, 0), gauge.size), GaugeColor::Background));

    GaugeComponent label = textComponent(GaugeComponent::Label, QPointF(20, 28), 22, Qt::AlignLeft | Qt::AlignVCenter,
                                         GaugeColor::Accent2);
    label.upperCase = true;
    gauge.components.append(label);
    GaugeComponent value = textComponent(GaugeComponent::ValueAndUnit, QPointF(20, 72), 40, Qt::AlignLeft | Qt::AlignVCenter);
    value.bold = true;
    gauge.components.append(value);
    return gauge;
}
