#include "gaugerenderer.h"

#include "../telemetry/telemetrysession.h"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
// Scaled value of the component's channel; false when there is none at this time.
bool sample(const GaugeComponent &component, const TelemetrySession *session, double time, double *value)
{
    const Channel *channel = session ? session->channel(component.channel) : nullptr;
    if (!channel)
        {
            return false;
        }
    bool ok = false;
    const double stored = channel->valueAt(time, &ok);
    *value = stored * component.valueScale;
    return ok;
}

void drawText(QPainter &painter, const GaugeComponent &component, const TelemetrySession *session, double time)
{
    QString text = component.text;
    if (!component.channel.isEmpty())
        {
            double value = 0.0;
            const QString shown = sample(component, session, time, &value)
                                  ? QString::number(value, 'f', component.decimals)
                                  : component.noValueText;
            text = text.isEmpty() ? shown : text.arg(shown);
        }
    QFont font = painter.font();
    if (!component.fontFamily.isEmpty())
        {
            font.setFamily(component.fontFamily);
        }
    font.setPixelSize(std::max(1, int(std::lround(component.fontSize))));
    font.setBold(component.bold);
    painter.setFont(font);
    painter.setPen(component.color);
    painter.drawText(component.rect, int(component.alignment) | Qt::TextDontClip, text);
}

void drawRotated(QPainter &painter, const GaugeComponent &component, const TelemetrySession *session, double time)
{
    double value = 0.0;
    if (!sample(component, session, time, &value))
        {
            value = component.minValue;
        }
    painter.save();
    painter.translate(component.pivot);
    painter.rotate(GaugeRenderer::angleFor(component, value));
    painter.translate(-component.pivot);
    painter.drawImage(component.rect, component.image);
    painter.restore();
}
}

double GaugeRenderer::angleFor(const GaugeComponent &component, double value)
{
    const double span = component.maxValue - component.minValue;
    if (span == 0.0 || std::isnan(value))
        {
            return component.minAngle;
        }
    const double f = std::clamp((value - component.minValue) / span, 0.0, 1.0);
    return component.minAngle + f * (component.maxAngle - component.minAngle);
}

void GaugeRenderer::render(QPainter &painter, const Gauge &gauge, const QRectF &target,
                           const TelemetrySession *session, double time)
{
    if (gauge.size.isEmpty() || target.isEmpty())
        {
            return;
        }
    painter.save();
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    painter.translate(target.topLeft());
    painter.scale(target.width() / gauge.size.width(), target.height() / gauge.size.height());
    for (const GaugeComponent &component : gauge.components)
        {
            switch (component.type)
                {
                case GaugeComponent::Image:
                    painter.drawImage(component.rect, component.image);
                    break;
                case GaugeComponent::Text:
                    drawText(painter, component, session, time);
                    break;
                case GaugeComponent::DynamicRotate:
                    drawRotated(painter, component, session, time);
                    break;
                }
        }
    painter.restore();
}
