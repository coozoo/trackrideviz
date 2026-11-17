#include "gaugeshape.h"

#include <algorithm>
#include <cmath>

namespace
{
QRectF across(const QRectF &rect, double factor)
{
    return QRectF(rect.x() * factor, rect.y(), rect.width() * factor, rect.height());
}

void widen(QVector<GaugeComponent> &components, double factor)
{
    for (GaugeComponent &c : components)
        {
            c.frame = across(c.frame, factor);
            for (QPointF &point : c.points)
                {
                    point.setX(point.x() * factor);
                }
            c.gradientLine = QLineF(c.gradientLine.x1() * factor, c.gradientLine.y1(),
                                    c.gradientLine.x2() * factor, c.gradientLine.y2());
            for (GaugeManipulation &m : c.manipulations)
                {
                    m.pivot.setX(m.pivot.x() * factor);
                    m.minX *= factor;
                    m.maxX *= factor;
                    if (!m.clipRect.isNull())
                        {
                            m.clipRect = across(m.clipRect, factor);
                        }
                    if (!m.frameRect.isNull())
                        {
                            m.frameRect = across(m.frameRect, factor);
                        }
                }
            // a tape lying across the gauge slides over the new width
            if (c.type == GaugeComponent::Tape
                    && (c.scale.orientation == GaugeScale::NumbersAbove || c.scale.orientation == GaugeScale::NumbersBelow))
                {
                    c.scale.length *= factor;
                    c.scale.spacing *= factor;
                }
            widen(c.children, factor);
        }
}

void scaleTexts(QVector<GaugeComponent> &components, double factor)
{
    for (GaugeComponent &c : components)
        {
            c.fontSize *= factor;
            for (QVector<GaugeComponent> *inside : {&c.children, &c.knob, &c.xMarker, &c.yMarker, &c.startLine})
                {
                    scaleTexts(*inside, factor);
                }
        }
}
}

Gauge GaugeShape::widened(const Gauge &gauge, double aspect)
{
    if (aspect <= 0.0 || gauge.isEmpty())
        {
            return gauge;
        }
    const double factor = std::clamp(aspect * gauge.size.height() / gauge.size.width(), 0.05, 50.0);
    if (std::abs(factor - 1.0) < 1e-6)
        {
            return gauge;
        }
    Gauge result = gauge;
    result.size.setWidth(gauge.size.width() * factor);
    widen(result.components, factor);
    return result;
}

Gauge GaugeShape::textsScaled(const Gauge &gauge, double factor)
{
    if (factor <= 0.0 || std::abs(factor - 1.0) < 1e-6)
        {
            return gauge;
        }
    Gauge result = gauge;
    scaleTexts(result.components, factor);
    return result;
}
