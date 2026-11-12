#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

// A gauge is a stack of simple components drawn back to front in its own
// "design" coordinate system (Gauge::size); the renderer scales that to
// wherever the gauge is placed on the video.
//
// The component set follows the old editor's gauge format (image, text,
// dynamicRotate, ...), so imported gauges map one to one.

struct GaugeComponent
{
    enum Type
    {
        Image,         // static picture: dial face, logo
        Text,          // static label or a channel value
        DynamicRotate  // picture turned around a pivot by a channel value: needle
    };

    Type type{Image};
    QRectF rect;            // design units
    QImage image;           // Image, DynamicRotate

    // Data binding (Text, DynamicRotate). Empty = static.
    QString channel;
    double valueScale{1.0}; // shown value = stored value * valueScale (3.6: m/s -> km/h)

    // Text
    QString text;           // static text, or a pattern with %1 for the value
    int decimals{0};
    QString noValueText{QStringLiteral("--")}; // shown where the channel has no data
    QColor color{Qt::white};
    QString fontFamily;     // empty = application font
    double fontSize{24.0};  // pixel size in design units
    bool bold{false};
    Qt::Alignment alignment{Qt::AlignCenter};

    // DynamicRotate: minValue..maxValue (after valueScale) maps linearly to
    // minAngle..maxAngle, degrees clockwise; values outside are clamped.
    QPointF pivot;          // design units
    double minValue{0.0};
    double maxValue{100.0};
    double minAngle{0.0};
    double maxAngle{360.0};
};

struct Gauge
{
    QString name;
    QSizeF size{400.0, 400.0}; // design units
    QVector<GaugeComponent> components;
};
