#pragma once

#include "../gauge/gaugetheme.h"
#include "overlay.h"

#include <QCoreApplication>

// A ready-made layout: which gauges, showing what, where, in which colors.
//
// Positions are kept the way .vet template files keep them: in pixels of a 1920 x 1080
// frame, measured from the left, middle or right edge (top, middle, bottom).
// Applied to a video of another shape, a gauge keeps its distance to its
// edge and its size relative to the picture height.
struct TemplateItem
{
    enum Edge { Start, Middle, End };   // left / top, center, right / bottom

    QString gaugeId;
    QString channel;
    Edge edgeX{Start};
    Edge edgeY{Start};
    double offsetX{0.0};     // reference pixels from the edge to the gauge's left side
    double offsetY{0.0};     // ... to its top
    double height{0.0};      // reference pixels
    OverlayItem::Streets streets{OverlayItem::AsDesigned};
    double closeUp{-1.0};    // as OverlayItem's
    int headingUp{-1};
    OverlayItem::UnitSystem units{OverlayItem::AsSettings};

    bool operator==(const TemplateItem &other) const
    {
        return gaugeId == other.gaugeId && channel == other.channel && edgeX == other.edgeX && edgeY == other.edgeY
               && offsetX == other.offsetX && offsetY == other.offsetY && height == other.height
               && streets == other.streets && closeUp == other.closeUp && headingUp == other.headingUp
               && units == other.units;
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
    // The other way: what is on a frame of that shape, as a template (without a name).
    // A gauge is measured from the edge its middle is nearest to - left, middle or right
    // third, top, middle or bottom - so that it keeps its corner on a frame of another shape.
    static GaugeTemplate fromOverlay(const Overlay &overlay, const GaugeTheme &theme, double frameAspect);
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
