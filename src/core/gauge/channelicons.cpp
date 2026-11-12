#include "channelicons.h"

#include "../telemetry/channelids.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace
{
const double kPi = 3.14159265358979323846;

// Everything is drawn in a 100 x 100 box and scaled.
QPainterPath heart()
{
    QPainterPath path;
    path.moveTo(50, 88);
    path.cubicTo(20, 66, 6, 50, 8, 32);
    path.cubicTo(10, 14, 36, 8, 50, 28);
    path.cubicTo(64, 8, 90, 14, 92, 32);
    path.cubicTo(94, 50, 80, 66, 50, 88);
    return path;
}

QPainterPath mountain()
{
    QPainterPath path;
    path.moveTo(4, 86);
    path.lineTo(38, 24);
    path.lineTo(54, 52);
    path.lineTo(66, 36);
    path.lineTo(96, 86);
    path.closeSubpath();
    return path;
}

QPainterPath bolt()
{
    QPainterPath path;
    path.moveTo(60, 4);
    path.lineTo(20, 56);
    path.lineTo(46, 56);
    path.lineTo(38, 96);
    path.lineTo(80, 40);
    path.lineTo(54, 40);
    path.closeSubpath();
    return path;
}

QPainterPath slope()
{
    QPainterPath path;
    path.moveTo(6, 84);
    path.lineTo(94, 84);
    path.lineTo(94, 22);
    path.closeSubpath();
    return path;
}

QPainterPath compassNeedle()
{
    QPainterPath path;
    path.moveTo(50, 6);
    path.lineTo(70, 74);
    path.lineTo(50, 60);
    path.lineTo(30, 74);
    path.closeSubpath();
    return path;
}

void drawIcon(QPainter &painter, const QString &id)
{
    const QPen thick(Qt::white, 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    if (id == ChannelId::HeartRate)
        {
            painter.drawPath(heart());
        }
    else if (id == ChannelId::Elevation)
        {
            painter.drawPath(mountain());
        }
    else if (id == ChannelId::Power)
        {
            painter.drawPath(bolt());
        }
    else if (id == ChannelId::Grade)
        {
            painter.drawPath(slope());
        }
    else if (id == ChannelId::Heading)
        {
            painter.drawPath(compassNeedle());
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 6));
            painter.drawEllipse(QPointF(50, 50), 44, 44);
        }
    else if (id == ChannelId::Temperature)
        {
            // thermometer: a bulb with a stem
            painter.drawEllipse(QPointF(50, 76), 17, 17);
            painter.drawRoundedRect(QRectF(41, 8, 18, 70), 9, 9);
        }
    else if (id == ChannelId::Cadence)
        {
            // chainring with a crank arm
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 8));
            painter.drawEllipse(QPointF(46, 54), 30, 30);
            painter.setPen(thick);
            painter.drawLine(QPointF(46, 54), QPointF(84, 16));
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPointF(84, 16), 9, 9);
        }
    else if (id == ChannelId::Distance)
        {
            // a flag on a pole
            painter.setPen(thick);
            painter.drawLine(QPointF(22, 94), QPointF(22, 8));
            painter.setPen(Qt::NoPen);
            QPainterPath flag;
            flag.moveTo(26, 10);
            flag.lineTo(86, 28);
            flag.lineTo(26, 48);
            flag.closeSubpath();
            painter.drawPath(flag);
        }
    else if (id == ChannelId::Speed)
        {
            // speedometer: an arc of three quarters and a needle
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 9, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(QRectF(10, 14, 80, 80), -45 * 16, 270 * 16);
            painter.setPen(thick);
            const double angle = 40.0 * kPi / 180.0;
            painter.drawLine(QPointF(50, 54), QPointF(50 + 32 * std::sin(angle), 54 - 32 * std::cos(angle)));
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPointF(50, 54), 8, 8);
        }
    else
        {
            // anything else: a plain dot
            painter.drawEllipse(QPointF(50, 50), 26, 26);
        }
}
}

QImage ChannelIcons::icon(const QString &channelId, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 100.0, size / 100.0);
    drawIcon(painter, channelId);
    return image;
}
