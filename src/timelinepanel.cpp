#include "timelinepanel.h"

#include "telemetry/channelids.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <functional>
#include <limits>

namespace
{
const int kRulerHeight = 16;
const int kVideoHeight = 8;
const int kLapsHeight = 18;
const int kMargin = 4;

QString clock(double seconds)
{
    const QString sign = seconds < 0.0 ? QStringLiteral("-") : QString();
    const qint64 whole = qint64(std::llround(std::fabs(seconds)));
    const qint64 hours = whole / 3600;
    return hours > 0 ? QString("%1%2:%3:%4").arg(sign).arg(hours).arg(whole / 60 % 60, 2, 10, QLatin1Char('0'))
           .arg(whole % 60, 2, 10, QLatin1Char('0'))
           : QString("%1%2:%3").arg(sign).arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}
}

// The drawing itself. Time on it is video time; data at telemetry time t is at t - offset.
class TimelineStrip : public QWidget
{
public:
    explicit TimelineStrip(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    std::function<void(double)> seek;
    std::function<void(double)> dragOffset;

    void setSession(const TelemetrySessionPtr &session)
    {
        m_session = session;
        fitRange();
    }

    void setTrace(const QString &channelId, const ChannelDisplay &display)
    {
        m_traceId = channelId;
        m_display = display;
        m_traceMin = std::numeric_limits<double>::max();
        m_traceMax = std::numeric_limits<double>::lowest();
        const Channel *channel = m_session && !channelId.isEmpty() ? m_session->channel(channelId) : nullptr;
        if (channel)
            {
                for (int i = 0; i < channel->count(); ++i)
                    {
                        const double v = display.convert(channel->valueAtIndex(i));
                        m_traceMin = std::min(m_traceMin, v);
                        m_traceMax = std::max(m_traceMax, v);
                    }
            }
        update();
    }

    void setVideoDuration(double seconds)
    {
        m_videoDuration = seconds;
        fitRange();
    }

    void setOffset(double seconds)
    {
        m_offset = seconds;
        // while dragging the view stays put, or the data would not move under the mouse
        if (!m_dragging)
            {
                fitRange();
            }
        update();
    }

    void setPosition(double seconds)
    {
        m_position = seconds;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        if (m_end <= m_start)
            {
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(rect(), Qt::AlignCenter, TimelinePanel::tr("Open a video or data"));
                return;
            }
        drawRuler(painter);
        const QRect video(QPoint(int(xOf(0.0)), kRulerHeight), QPoint(int(xOf(m_videoDuration)), kRulerHeight + kVideoHeight - 2));
        if (m_videoDuration > 0.0)
            {
                painter.fillRect(video, palette().color(QPalette::Mid));
            }
        const QRect trace = traceRect();
        const QRect laps(0, trace.bottom() + 2, width(), kLapsHeight);
        if (m_session)
            {
                painter.setRenderHint(QPainter::Antialiasing);
                drawPauses(painter, trace);
                drawTrace(painter, trace);
                painter.setRenderHint(QPainter::Antialiasing, false);
                drawLaps(painter, laps);
            }
        // the playhead
        const double x = xOf(m_position);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        if (m_session && !m_traceId.isEmpty())
            {
                const Channel *channel = m_session->channel(m_traceId);
                bool ok = false;
                const double value = channel ? channel->valueAt(m_position + m_offset, &ok) : 0.0;
                const QString text = ok ? QString("%1 %2").arg(m_display.convert(value), 0, 'f', m_display.decimals)
                                     .arg(m_display.unit).trimmed() : QStringLiteral("--");
                painter.setPen(palette().color(QPalette::Text));
                const int w = fontMetrics().horizontalAdvance(text) + 8;
                const bool left = x + w > width();
                painter.drawText(QRectF(left ? x - w : x + 4, trace.top(), w - 4, fontMetrics().height()),
                                 left ? Qt::AlignRight : Qt::AlignLeft, text);
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_end <= m_start)
            {
                return;
            }
        m_pressX = event->position().x();
        m_pressOffset = m_offset;
        m_dragging = (event->modifiers() & Qt::ControlModifier) && m_session;
        if (!m_dragging)
            {
                seekTo(event->position().x());
            }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            {
                setCursor((event->modifiers() & Qt::ControlModifier) && m_session ? Qt::SizeHorCursor : Qt::ArrowCursor);
                return;
            }
        if (m_dragging)
            {
                // the data follows the mouse: later in the video means a smaller offset
                const double shift = (event->position().x() - m_pressX) / pixelsPerSecond();
                if (dragOffset)
                    {
                        dragOffset(std::round((m_pressOffset - shift) * 100.0) / 100.0);
                    }
            }
        else
            {
                seekTo(event->position().x());
            }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (m_dragging)
            {
                m_dragging = false;
                fitRange();
            }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_end <= m_start)
            {
                return;
            }
        const double steps = event->angleDelta().y() / 120.0;
        const double sideways = event->angleDelta().x() / 120.0;
        const double span = m_viewEnd - m_viewStart;
        if (steps != 0.0 && !(event->modifiers() & Qt::ShiftModifier))
            {
                // about the time under the mouse
                const double at = timeOf(event->position().x());
                const double factor = std::pow(0.8, steps);
                const double newSpan = std::clamp(span * factor, 5.0, m_end - m_start);
                const double fraction = (at - m_viewStart) / span;
                setView(at - fraction * newSpan, newSpan);
            }
        else
            {
                const double pan = (sideways != 0.0 ? sideways : steps) * span * 0.1;
                setView(m_viewStart - pan, span);
            }
        event->accept();
    }

private:
    QRect traceRect() const
    {
        const int top = kRulerHeight + kVideoHeight;
        return QRect(0, top + kMargin, width(), std::max(10, height() - top - kLapsHeight - 2 * kMargin - 2));
    }

    double pixelsPerSecond() const { return width() / std::max(m_viewEnd - m_viewStart, 1e-6); }
    double xOf(double t) const { return (t - m_viewStart) * pixelsPerSecond(); }
    double timeOf(double x) const { return m_viewStart + x / pixelsPerSecond(); }

    void seekTo(double x)
    {
        if (seek && m_videoDuration > 0.0)
            {
                seek(std::clamp(timeOf(x), 0.0, m_videoDuration));
            }
    }

    // everything: the video and the data where the offset puts it
    void fitRange()
    {
        const double span = m_viewEnd - m_viewStart;
        const bool whole = span <= 0.0 || span >= m_end - m_start - 1e-6;
        m_start = 0.0;
        m_end = m_videoDuration;
        if (m_session && m_session->duration() > 0.0)
            {
                m_start = std::min(m_start, -m_offset);
                m_end = std::max(m_end, m_session->duration() - m_offset);
            }
        if (whole)
            {
                m_viewStart = m_start;
                m_viewEnd = m_end;
            }
        else
            {
                setView(m_viewStart, span);
            }
        update();
    }

    void setView(double start, double span)
    {
        span = std::min(span, m_end - m_start);
        m_viewStart = std::clamp(start, m_start, m_end - span);
        m_viewEnd = m_viewStart + span;
        update();
    }

    void drawRuler(QPainter &painter)
    {
        // a step that leaves room for a label
        const double minimum = 70.0 / pixelsPerSecond();
        double step = 1.0;
        for (const double s : {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0, 7200.0})
            {
                step = s;
                if (s >= minimum)
                    {
                        break;
                    }
            }
        QColor grid = palette().color(QPalette::Text);
        grid.setAlpha(40);
        for (double t = std::ceil(m_viewStart / step) * step; t <= m_viewEnd; t += step)
            {
                const double x = xOf(t);
                painter.setPen(grid);
                painter.drawLine(QPointF(x, kRulerHeight - 4), QPointF(x, height()));
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(QPointF(x + 2, kRulerHeight - 5), clock(t));
            }
    }

    void drawPauses(QPainter &painter, const QRect &area)
    {
        QColor colour = palette().color(QPalette::Text);
        colour.setAlpha(60);
        for (const TelemetrySession::Pause &pause : m_session->pauses())
            {
                const double x0 = xOf(pause.startTime - m_offset);
                const double x1 = xOf(pause.endTime - m_offset);
                painter.fillRect(QRectF(x0, area.top(), std::max(1.0, x1 - x0), area.height()),
                                 QBrush(colour, Qt::BDiagPattern));
            }
    }

    bool inPause(double t) const
    {
        for (const TelemetrySession::Pause &pause : m_session->pauses())
            {
                if (t > pause.startTime && t < pause.endTime)
                    {
                        return true;
                    }
            }
        return false;
    }

    void drawTrace(QPainter &painter, const QRect &area)
    {
        const Channel *channel = m_traceId.isEmpty() ? nullptr : m_session->channel(m_traceId);
        if (!channel || channel->isEmpty())
            {
                return;
            }
        // a flat line in the middle when nothing changes
        const double low = m_traceMax > m_traceMin ? m_traceMin : m_traceMin - 1.0;
        const double high = m_traceMax > m_traceMin ? m_traceMax : m_traceMax + 1.0;
        const double first = channel->timeAt(0);
        const double last = channel->timeAt(channel->count() - 1);
        QPainterPath line;
        bool drawing = false;
        for (int x = 0; x <= width(); ++x)
            {
                const double t = timeOf(x) + m_offset;
                bool ok = false;
                const double v = t >= first && t <= last && !inPause(t) ? channel->valueAt(t, &ok) : 0.0;
                if (!ok)
                    {
                        drawing = false;
                        continue;
                    }
                const double y = area.bottom() - (m_display.convert(v) - low) / (high - low) * area.height();
                if (drawing)
                    {
                        line.lineTo(x, y);
                    }
                else
                    {
                        line.moveTo(x, y);
                        drawing = true;
                    }
            }
        painter.setPen(QPen(palette().color(QPalette::Text), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(line);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(area.adjusted(4, 0, -4, 0), Qt::AlignTop | Qt::AlignLeft,
                         QString::number(high, 'f', m_display.decimals));
        painter.drawText(area.adjusted(4, 0, -4, 0), Qt::AlignBottom | Qt::AlignLeft,
                         QString::number(low, 'f', m_display.decimals));
    }

    void drawLaps(QPainter &painter, const QRect &row)
    {
        const QVector<TelemetrySession::Lap> &laps = m_session->laps();
        int best = -1;
        for (int i = 0; i < laps.size(); ++i)
            {
                const double duration = laps.at(i).endTime - laps.at(i).startTime;
                if (best < 0 || duration < laps.at(best).endTime - laps.at(best).startTime)
                    {
                        best = i;
                    }
            }
        QColor even = palette().color(QPalette::Highlight);
        even.setAlpha(110);
        QColor odd = even;
        odd.setAlpha(60);
        for (int i = 0; i < laps.size(); ++i)
            {
                const TelemetrySession::Lap &lap = laps.at(i);
                const QRectF band(QPointF(xOf(lap.startTime - m_offset), row.top()),
                                  QPointF(xOf(lap.endTime - m_offset), row.bottom()));
                if (band.right() < 0 || band.left() > width())
                    {
                        continue;
                    }
                painter.fillRect(band, i % 2 == 0 ? even : odd);
                painter.setPen(palette().color(QPalette::Text));
                for (const double split : lap.splits)
                    {
                        const double x = xOf(split - m_offset);
                        painter.drawLine(QPointF(x, row.top() + row.height() / 2.0), QPointF(x, row.bottom()));
                    }
                if (i == best && laps.size() > 1)
                    {
                        painter.setPen(QPen(QColor(150, 60, 200), 2));
                        painter.drawRect(band.adjusted(1, 1, -1, -1));
                    }
                // as much as there is room for
                const double duration = lap.endTime - lap.startTime;
                const QString full = TimelinePanel::tr("Lap %1  %2").arg(i + 1)
                                     .arg(QString("%1:%2").arg(int(duration / 60)).arg(std::fmod(duration, 60.0), 6, 'f', 3, QLatin1Char('0')));
                const QString shortText = QString::number(i + 1);
                const QString text = fontMetrics().horizontalAdvance(full) + 6 < band.width() ? full
                                     : fontMetrics().horizontalAdvance(shortText) + 4 < band.width() ? shortText : QString();
                painter.setPen(palette().color(QPalette::Text));
                painter.drawText(band.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
            }
    }

    TelemetrySessionPtr m_session;
    QString m_traceId;
    ChannelDisplay m_display;
    double m_traceMin{0.0};
    double m_traceMax{0.0};
    double m_videoDuration{0.0};
    double m_offset{0.0};
    double m_position{0.0};
    double m_start{0.0};          // all there is, video time
    double m_end{0.0};
    double m_viewStart{0.0};      // what is shown of it
    double m_viewEnd{0.0};
    bool m_dragging{false};
    double m_pressX{0.0};
    double m_pressOffset{0.0};
};

TimelinePanel::TimelinePanel(QWidget *parent)
    : QWidget(parent)
{
    m_strip = new TimelineStrip(this);
    m_strip->seek = [this](double t) { emit seekRequested(t); };
    m_strip->dragOffset = [this](double offset) { emit offsetDragged(offset); };

    m_channelCombo = new QComboBox(this);
    m_channelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_channelCombo->setToolTip(tr("The measurement drawn along the timeline"));
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int)
    {
        const QString id = m_channelCombo->currentData().toString();
        const Channel *channel = m_session ? m_session->channel(id) : nullptr;
        m_strip->setTrace(id, channel ? ChannelFormat::displayFor(*channel, m_units) : ChannelDisplay());
    });
    QLabel *hint = new QLabel(tr("Click to go there, wheel to zoom, Ctrl+drag to slide the data against the video"), this);
    hint->setForegroundRole(QPalette::PlaceholderText);

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Show:"), this));
    top->addWidget(m_channelCombo);
    top->addSpacing(12);
    top->addWidget(hint, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addLayout(top);
    layout->addWidget(m_strip, 1);
}

void TimelinePanel::setSession(const TelemetrySessionPtr &session, const Units &units)
{
    m_session = session;
    m_units = units;
    m_strip->setSession(session);
    fillChannels();
}

void TimelinePanel::fillChannels()
{
    const QString current = m_channelCombo->currentData().toString();
    m_channelCombo->clear();
    if (m_session)
        {
            for (const QString &id : m_session->channelIds())
                {
                    const Channel *channel = m_session->channel(id);
                    // positions say nothing as a line over time
                    if (channel && !channel->isEmpty() && id != ChannelId::PositionLat && id != ChannelId::PositionLon)
                        {
                            m_channelCombo->addItem(ChannelFormat::displayFor(*channel, m_units).name, id);
                        }
                }
        }
    int row = m_channelCombo->findData(current);
    if (row < 0)
        {
            row = std::max(m_channelCombo->findData(QString(ChannelId::Speed)), 0);
        }
    m_channelCombo->setCurrentIndex(row);
    const QString id = m_channelCombo->currentData().toString();
    const Channel *channel = m_session ? m_session->channel(id) : nullptr;
    m_strip->setTrace(id, channel ? ChannelFormat::displayFor(*channel, m_units) : ChannelDisplay());
}

void TimelinePanel::setVideoDuration(double seconds)
{
    m_strip->setVideoDuration(seconds);
}

void TimelinePanel::setOffset(double seconds)
{
    m_strip->setOffset(seconds);
}

void TimelinePanel::setPosition(double videoSeconds)
{
    m_strip->setPosition(videoSeconds);
}
