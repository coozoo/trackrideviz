#include "channelgraph.h"

#include "perfmeter.h"
#include "settings/appsettings.h"
#include "units/units.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <limits>

namespace
{
const double kMargin = 4.0;     // above and below the line, pixels
}

ChannelGraph::ChannelGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
    setAntialiasing(true);
    connect(this, &ChannelGraph::colorsChanged, this, [this]() { update(); });
    connect(this, &QQuickItem::widthChanged, this, &ChannelGraph::stale);
}

void ChannelGraph::setController(SimpleController *controller)
{
    if (controller == m_controller)
        {
            return;
        }
    if (m_controller)
        {
            disconnect(m_controller, nullptr, this, nullptr);
        }
    m_controller = controller;
    if (m_controller)
        {
            // other data, its laps, the data moved against the video
            connect(m_controller, &SimpleController::dataChanged, this, &ChannelGraph::stale);
            connect(m_controller, &SimpleController::lapsChanged, this, &ChannelGraph::stale);
            connect(m_controller, &SimpleController::syncChanged, this, &ChannelGraph::stale);
        }
    stale();
    emit controllerChanged();
}

void ChannelGraph::setChannel(const QString &channel)
{
    if (channel == m_channel)
        {
            return;
        }
    m_channel = channel;
    stale();
    emit channelChanged();
}

void ChannelGraph::setPosition(qint64 positionMs)
{
    if (positionMs == m_position)
        {
            return;
        }
    m_position = positionMs;
    emit positionChanged();
    update();
}

void ChannelGraph::setDuration(qint64 durationMs)
{
    if (durationMs == m_duration)
        {
            return;
        }
    m_duration = durationMs;
    stale();
    emit durationChanged();
}

void ChannelGraph::stale()
{
    m_sampledColumns = 0;
    update();
}

void ChannelGraph::sample(int columns)
{
    m_values.fill(std::numeric_limits<double>::quiet_NaN(), columns);
    m_sampledColumns = columns;
    m_low = std::numeric_limits<double>::max();
    m_high = std::numeric_limits<double>::lowest();
    const TelemetrySessionPtr session = m_controller ? m_controller->session() : TelemetrySessionPtr();
    const Channel *channel = session && !m_channel.isEmpty() ? session->channel(m_channel) : nullptr;
    if (!channel || channel->isEmpty() || m_duration <= 0 || columns <= 0)
        {
            return;
        }
    for (int x = 0; x < columns; ++x)
        {
            const qint64 videoMs = qint64(std::llround((x + 0.5) / columns * double(m_duration)));
            bool ok = false;
            const double value = channel->valueAt(m_controller->telemetryTime(videoMs), &ok);
            if (ok && std::isfinite(value))
                {
                    m_values[x] = value;
                    m_low = std::min(m_low, value);
                    m_high = std::max(m_high, value);
                }
        }
}

void ChannelGraph::paint(QPainter *painter)
{
    static PerfMeter meter("graph painted");
    const PerfMeter::Scope timed(meter);
    const int columns = int(std::floor(width()));
    if (!m_controller || columns <= 0 || height() <= 0.0 || m_duration <= 0)
        {
            return;
        }
    if (m_sampledColumns != columns)
        {
            sample(columns);
        }
    painter->setRenderHint(QPainter::Antialiasing);
    const double msPerPixel = double(m_duration) / width();
    const TelemetrySessionPtr session = m_controller->session();

    // the laps: every other one in a faint band
    if (session)
        {
            const SyncMap sync = m_controller->project().sync();
            QColor band = m_textColor;
            band.setAlphaF(0.08);
            const QVector<TelemetrySession::Lap> &laps = session->laps();
            for (int i = 0; i < laps.size(); i += 2)
                {
                    const double from = sync.videoTime(laps.at(i).startTime) * 1000.0 / msPerPixel;
                    const double to = sync.videoTime(laps.at(i).endTime) * 1000.0 / msPerPixel;
                    painter->fillRect(QRectF(from, 0.0, std::max(1.0, to - from), height()), band);
                }
        }

    // the line, from the lowest to the highest the video shows
    if (m_high >= m_low)
        {
            const double span = std::max(m_high - m_low, 1e-9);
            const double top = kMargin;
            const double room = height() - 2.0 * kMargin;
            QPainterPath line;
            bool drawing = false;
            for (int x = 0; x < m_values.size(); ++x)
                {
                    const double value = m_values.at(x);
                    if (std::isnan(value))
                        {
                            drawing = false;
                            continue;
                        }
                    const QPointF point(x + 0.5, top + room * (1.0 - (value - m_low) / span));
                    drawing ? line.lineTo(point) : line.moveTo(point);
                    drawing = true;
                }
            painter->setPen(QPen(m_lineColor, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(line);
        }

    // where the video is, and the value there
    const double x = m_position / msPerPixel;
    painter->setPen(QPen(m_cursorColor, 2.0));
    painter->drawLine(QPointF(x, 0.0), QPointF(x, height()));
    const Channel *channel = session && !m_channel.isEmpty() ? session->channel(m_channel) : nullptr;
    if (channel)
        {
            bool ok = false;
            const double value = channel->valueAt(m_controller->telemetryTime(m_position), &ok);
            const ChannelDisplay display = ChannelFormat::displayFor(m_channel, channel->unit(), AppSettings::units());
            const double shown = display.convert(value);
            const QString text = ok ? QString("%1 %2").arg(display.text(shown), display.unit).trimmed() : QStringLiteral("--");
            const QFontMetricsF metrics(painter->font());
            const double w = metrics.horizontalAdvance(text) + 8.0;
            const bool left = x + w > width();
            painter->setPen(m_textColor);
            painter->drawText(QRectF(left ? x - w : x + 4.0, 0.0, w - 4.0, metrics.height()),
                              left ? Qt::AlignRight : Qt::AlignLeft, text);
        }
}
