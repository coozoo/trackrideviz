#include "builtingauges.h"

#include "../telemetry/channelids.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace
{
const double kSize = 400.0;
// The scale runs clockwise from lower left to lower right; 0 degrees is 12 o'clock.
const double kMinAngle = -135.0;
const double kMaxAngle = 135.0;
const double kPi = 3.14159265358979323846;

QImage blankImage()
{
    const int side = int(kSize);
    QImage image(QSize(side, side), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

// Point on a circle around the dial centre; angle in degrees clockwise from 12 o'clock.
QPointF onDial(double angle, double radius)
{
    const double rad = angle * kPi / 180.0;
    return QPointF(kSize / 2 + radius * std::sin(rad), kSize / 2 - radius * std::cos(rad));
}

QImage dialFace(int maxSpeed)
{
    QImage image = blankImage();
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const QPointF centre(kSize / 2, kSize / 2);
    painter.setPen(QPen(QColor(255, 255, 255, 200), 4));
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawEllipse(centre, kSize / 2 - 4, kSize / 2 - 4);

    QFont font = painter.font();
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    const int minorStep = maxSpeed <= 100 ? 5 : 10;
    const int majorStep = minorStep * 2;
    for (int value = 0; value <= maxSpeed; value += minorStep)
        {
            const double angle = kMinAngle + (kMaxAngle - kMinAngle) * value / maxSpeed;
            const bool major = value % majorStep == 0;
            painter.setPen(QPen(Qt::white, major ? 5 : 2, Qt::SolidLine, Qt::FlatCap));
            painter.drawLine(onDial(angle, major ? 158 : 170), onDial(angle, 186));
            if (major)
                {
                    QRectF box(0, 0, 70, 34);
                    box.moveCenter(onDial(angle, 128));
                    painter.drawText(box, Qt::AlignCenter, QString::number(value));
                }
        }
    return image;
}

// Drawn pointing at 12 o'clock; the renderer turns it around the dial centre.
QImage needle()
{
    QImage image = blankImage();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double c = kSize / 2;
    QPainterPath path;
    path.moveTo(c, c - 176);
    path.lineTo(c + 7, c + 30);
    path.lineTo(c - 7, c + 30);
    path.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 80, 40));
    painter.drawPath(path);
    painter.setBrush(QColor(40, 40, 40));
    painter.setPen(QPen(Qt::white, 3));
    painter.drawEllipse(QPointF(c, c), 16, 16);
    return image;
}
}

Gauge BuiltinGauges::speedometer(const Units &units)
{
    const int maxSpeed = units.speed == Units::MilesPerHour ? 50 : 80;
    Gauge gauge;
    gauge.name = "Speedometer";
    gauge.size = QSizeF(kSize, kSize);
    const QRectF whole(0, 0, kSize, kSize);

    GaugeComponent face;
    face.type = GaugeComponent::Image;
    face.rect = whole;
    face.image = dialFace(maxSpeed);
    gauge.components.append(face);

    GaugeComponent value;
    value.type = GaugeComponent::Text;
    value.rect = QRectF(100, 262, 200, 70);
    value.channel = ChannelId::Speed;
    value.valueScale = units.speedScale();
    value.fontSize = 64;
    value.bold = true;
    gauge.components.append(value);

    GaugeComponent unit;
    unit.type = GaugeComponent::Text;
    unit.rect = QRectF(100, 328, 200, 30);
    unit.text = units.speedLabel();
    unit.fontSize = 24;
    unit.color = QColor(220, 220, 220);
    gauge.components.append(unit);

    GaugeComponent hand;
    hand.type = GaugeComponent::DynamicRotate;
    hand.rect = whole;
    hand.image = needle();
    hand.pivot = QPointF(kSize / 2, kSize / 2);
    hand.channel = ChannelId::Speed;
    hand.valueScale = units.speedScale();
    hand.minValue = 0;
    hand.maxValue = maxSpeed;
    hand.minAngle = kMinAngle;
    hand.maxAngle = kMaxAngle;
    gauge.components.append(hand);

    return gauge;
}

Gauge BuiltinGauges::byId(const QString &id, const Units &units)
{
    if (id == QLatin1String(SpeedometerId))
        {
            return speedometer(units);
        }
    return Gauge();
}
