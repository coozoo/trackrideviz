#pragma once

#include "../gauge/gaugetheme.h"
#include "overlay.h"

#include <QCoreApplication>

// A ready-made layout: which gauges, showing what, where, in which colours.
//
// Positions are kept the way the old editor keeps them: in pixels of a 1920 x 1080
// frame, measured from the left, middle or right edge (top, middle, bottom).
// Applied to a video of another shape, a gauge keeps its distance to its
// edge and its size relative to the picture height.
struct TemplateItem
{
    enum Edge { Start, Middle, End };   // left / top, centre, right / bottom

    QString gaugeId;
    QString channel;
    Edge edgeX{Start};
    Edge edgeY{Start};
    double offsetX{0.0};     // reference pixels from the edge to the gauge's left side
    double offsetY{0.0};     // ... to its top
    double height{0.0};      // reference pixels

    bool operator==(const TemplateItem &other) const
    {
        return gaugeId == other.gaugeId && channel == other.channel && edgeX == other.edgeX && edgeY == other.edgeY
               && offsetX == other.offsetX && offsetY == other.offsetY && height == other.height;
    }
};

struct GaugeTemplate
{
    static constexpr double ReferenceWidth = 1920.0;
    static constexpr double ReferenceHeight = 1080.0;

    QString name;
    QString category;
    GaugeTheme theme;
    QVector<TemplateItem> items;

    // Placement on a frame with this width / height ratio.
    QVector<OverlayItem> overlayItems(double frameAspect) const;
};

class TemplateFile
{
    Q_DECLARE_TR_FUNCTIONS(TemplateFile)

public:
    static QByteArray toJson(const GaugeTemplate &gaugeTemplate);
    static bool fromJson(const QByteArray &json, GaugeTemplate *gaugeTemplate, QString *error = nullptr);
    static bool save(const QString &path, const GaugeTemplate &gaugeTemplate, QString *error = nullptr);
    static bool load(const QString &path, GaugeTemplate *gaugeTemplate, QString *error = nullptr);
};
