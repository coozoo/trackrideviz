#pragma once

#include "gauge.h"

class QPainter;
class TelemetrySession;

// Draws a gauge with QPainter. The one render path: the preview canvas, the
// export worker and library thumbnails all call this, so what is seen while
// editing is what gets exported.
//
// Stateless and reentrant; needs a QGuiApplication for text.
class GaugeRenderer
{
public:
    // Draws `gauge` into `target` (painter coordinates) with the data at
    // `time` seconds from the session start. session may be nullptr: bound
    // components then draw their "no value" state.
    static void render(QPainter &painter, const Gauge &gauge, const QRectF &target,
                       const TelemetrySession *session, double time);

    // Angle of a DynamicRotate component for a (scaled) value.
    static double angleFor(const GaugeComponent &component, double value);
};
