#include "channelgraph.h"

#include "perfmeter.h"
#include "settings/appsettings.h"
#include "units/units.h"

#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>

#include <cmath>
#include <limits>

namespace
{
const double kMargin = 4.0;     // above and below the line, pixels
const double kCursorWidth = 2.0;
}

ChannelGraph::ChannelGraph(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    connect(this, &ChannelGraph::colorsChanged, this, &ChannelGraph::stale);
    connect(this, &QQuickItem::widthChanged, this, &ChannelGraph::stale);
    connect(this, &QQuickItem::heightChanged, this, &ChannelGraph::stale);
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
    updateValueText();
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

qreal ChannelGraph::cursorX() const
{
    return m_duration > 0 ? m_position * width() / double(m_duration) : 0.0;
}

void ChannelGraph::stale()
{
    m_sampledColumns = 0;
    m_background = QImage();
    updateValueText();
    update();
}

void ChannelGraph::updateValueText()
{
    const TelemetrySessionPtr session = m_controller ? m_controller->session() : TelemetrySessionPtr();
    const Channel *channel = session && !m_channel.isEmpty() ? session->channel(m_channel) : nullptr;
    QString text;
    if (channel)
        {
            bool ok = false;
            const double value = channel->valueAt(m_controller->telemetryTime(m_position), &ok);
            const ChannelDisplay display = ChannelFormat::displayFor(m_channel, channel->unit(), AppSettings::units());
            text = ok ? QString("%1 %2").arg(display.text(display.convert(value)), display.unit).trimmed()
                      : QStringLiteral("--");
        }
    if (text != m_valueText)
        {
            m_valueText = text;
            emit valueTextChanged();
        }
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

void ChannelGraph::paintBackground(QPainter &painter, double msPerPixel)
{
    painter.setRenderHint(QPainter::Antialiasing);
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
                    painter.fillRect(QRectF(from, 0.0, std::max(1.0, to - from), height()), band);
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
            painter.setPen(QPen(m_lineColor, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(line);
        }
}

QImage ChannelGraph::background(qreal ratio)
{
    const int columns = int(std::floor(width()));
    if (!m_controller || columns <= 0 || height() <= 0.0 || m_duration <= 0)
        {
            return QImage();
        }
    if (m_sampledColumns != columns)
        {
            sample(columns);
            m_background = QImage();
        }
    const QSize pixels = (size() * ratio).toSize();
    if (m_background.size() != pixels || m_background.devicePixelRatio() != ratio)
        {
            static PerfMeter meter("graph line drawn");
            const PerfMeter::Scope timed(meter);
            m_background = QImage(pixels, QImage::Format_RGBA8888_Premultiplied);
            m_background.setDevicePixelRatio(ratio);
            m_background.fill(Qt::transparent);
            QPainter painter(&m_background);
            paintBackground(painter, double(m_duration) / width());
            m_backgroundShown = false;
        }
    return m_background;
}

QSGNode *ChannelGraph::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    // the line's picture, then the cursor over it
    QSGNode *root = old;
    const QImage line = window() ? background(window()->effectiveDevicePixelRatio()) : QImage();
    if (line.isNull())
        {
            delete root;
            return nullptr;
        }
    if (!root)
        {
            root = new QSGNode;
            QSGImageNode *image = window()->createImageNode();
            image->setOwnsTexture(true);
            image->setFiltering(QSGTexture::Linear);
            root->appendChildNode(image);
            root->appendChildNode(window()->createRectangleNode());
            m_backgroundShown = false;
        }
    auto *image = static_cast<QSGImageNode *>(root->firstChild());
    if (!m_backgroundShown)
        {
            image->setTexture(window()->createTextureFromImage(line));
            m_backgroundShown = true;
        }
    image->setRect(boundingRect());
    auto *cursor = static_cast<QSGRectangleNode *>(root->lastChild());
    cursor->setColor(m_cursorColor);
    cursor->setRect(QRectF(cursorX() - kCursorWidth / 2.0, 0.0, kCursorWidth, height()));
    return root;
}
