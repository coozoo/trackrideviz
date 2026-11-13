#include "timelinepanel.h"

#include "telemetry/channelids.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QToolButton>
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

// The drawing itself. Time on it is video time; data at telemetry time t is where the sync map puts it.
class TimelineStrip : public QWidget
{
public:
    explicit TimelineStrip(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
    }

    std::function<void(double)> seek;
    std::function<void(double)> dragOffset;
    std::function<void()> marksChanged;
    std::function<void(const TimeRange &)> keepAgain;

    void setCuts(const QVector<TimeRange> &cuts)
    {
        m_cuts = cuts;
        update();
    }

    // -1: not set
    void setMark(bool b, double t)
    {
        (b ? m_markB : m_markA) = t;
        update();
        if (marksChanged)
            {
                marksChanged();
            }
    }

    // A..B in order; invalid (length 0) unless both are set
    TimeRange marked() const
    {
        if (m_markA < 0.0 || m_markB < 0.0)
            {
                return TimeRange();
            }
        return {std::min(m_markA, m_markB), std::max(m_markA, m_markB)};
    }

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

    void setSync(const SyncMap &sync)
    {
        m_sync = sync;
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

    double position() const { return m_position; }

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
        drawCuts(painter);
        // the playhead
        const double x = xOf(m_position);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        if (m_session && !m_traceId.isEmpty())
            {
                const Channel *channel = m_session->channel(m_traceId);
                bool ok = false;
                const double value = channel ? channel->valueAt(m_sync.telemetryTime(m_position), &ok) : 0.0;
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
        if (m_end <= m_start)
            {
                return;
            }
        const double t = std::clamp(timeOf(event->position().x()), 0.0, std::max(0.0, m_videoDuration));
        if (event->button() == Qt::RightButton)
            {
                const TimeRange *cut = CutList::cutAt(m_cuts, timeOf(event->position().x()));
                if (cut && keepAgain)
                    {
                        const TimeRange range = *cut;
                        QMenu menu(this);
                        menu.addAction(TimelinePanel::tr("Keep this part again"), this, [this, range]() { keepAgain(range); });
                        menu.exec(event->globalPosition().toPoint());
                    }
                return;
            }
        if (event->button() != Qt::LeftButton)
            {
                return;
            }
        m_pressX = event->position().x();
        m_pressOffset = m_sync.offset;
        m_dragging = (event->modifiers() & Qt::ControlModifier) && m_session;
        m_selecting = !m_dragging && (event->modifiers() & Qt::ShiftModifier) && m_videoDuration > 0.0;
        if (m_selecting)
            {
                m_markA = t;
                setMark(true, t);
            }
        else if (!m_dragging)
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
        if (m_selecting)
            {
                setMark(true, std::clamp(timeOf(event->position().x()), 0.0, m_videoDuration));
            }
        else if (m_dragging)
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
        m_selecting = false;
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
                m_start = std::min(m_start, m_sync.videoTime(0.0));
                m_end = std::max(m_end, m_sync.videoTime(m_session->duration()));
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

    // what is cut darkened and crossed out; the marks and what is between them
    void drawCuts(QPainter &painter)
    {
        const QRect all(0, kRulerHeight, width(), height() - kRulerHeight);
        for (const TimeRange &cut : m_cuts)
            {
                const QRectF band(QPointF(xOf(cut.start), all.top()), QPointF(xOf(cut.end), all.bottom()));
                if (band.right() < 0 || band.left() > width())
                    {
                        continue;
                    }
                painter.fillRect(band, QColor(0, 0, 0, 110));
                painter.fillRect(band, QBrush(QColor(220, 40, 40, 150), Qt::FDiagPattern));
                painter.fillRect(QRectF(band.left(), all.top(), band.width(), 3), QColor(220, 40, 40));
            }
        const TimeRange selection = marked();
        if (selection.length() > 0.0)
            {
                QColor fill = palette().color(QPalette::Highlight);
                fill.setAlpha(50);
                painter.fillRect(QRectF(QPointF(xOf(selection.start), all.top()), QPointF(xOf(selection.end), all.bottom())), fill);
            }
        painter.setPen(QPen(QColor(240, 170, 0), 2));
        for (const bool b : {false, true})
            {
                const double mark = b ? m_markB : m_markA;
                if (mark >= 0.0)
                    {
                        const double x = xOf(mark);
                        painter.drawLine(QPointF(x, all.top()), QPointF(x, all.bottom()));
                        painter.drawText(QPointF(x + 3, all.top() + fontMetrics().ascent()), b ? "B" : "A");
                    }
            }
    }

    void drawPauses(QPainter &painter, const QRect &area)
    {
        QColor colour = palette().color(QPalette::Text);
        colour.setAlpha(60);
        for (const TelemetrySession::Pause &pause : m_session->pauses())
            {
                const double x0 = xOf(m_sync.videoTime(pause.startTime));
                const double x1 = xOf(m_sync.videoTime(pause.endTime));
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
                const double t = m_sync.telemetryTime(timeOf(x));
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
                const QRectF band(QPointF(xOf(m_sync.videoTime(lap.startTime)), row.top()),
                                  QPointF(xOf(m_sync.videoTime(lap.endTime)), row.bottom()));
                if (band.right() < 0 || band.left() > width())
                    {
                        continue;
                    }
                painter.fillRect(band, i % 2 == 0 ? even : odd);
                painter.setPen(palette().color(QPalette::Text));
                for (const double split : lap.splits)
                    {
                        const double x = xOf(m_sync.videoTime(split));
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
    SyncMap m_sync;
    double m_position{0.0};
    double m_start{0.0};          // all there is, video time
    double m_end{0.0};
    double m_viewStart{0.0};      // what is shown of it
    double m_viewEnd{0.0};
    bool m_dragging{false};
    bool m_selecting{false};
    QVector<TimeRange> m_cuts;
    double m_markA{-1.0};
    double m_markB{-1.0};
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
    m_strip->setToolTip(tr("Click to go there, wheel to zoom, Ctrl+drag to slide the data against the video.\n"
                           "Shift+drag marks a part; right-click a cut part to keep it again."));
    m_strip->marksChanged = [this]() { updateCutButtons(); };
    m_strip->keepAgain = [this](const TimeRange &range)
    {
        emit cutsChanged(CutList::removed(m_cuts, range), tr("Keep part again"));
    };

    auto button = [this](const QString &text, const QString &tip)
    {
        QToolButton *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton *markA = button(tr("A"), tr("Mark the start of a part here ([)"));
    QToolButton *markB = button(tr("B"), tr("Mark the end of a part here (])"));
    connect(markA, &QToolButton::clicked, this, [this]() { m_strip->setMark(false, m_strip->position()); });
    connect(markB, &QToolButton::clicked, this, [this]() { m_strip->setMark(true, m_strip->position()); });
    for (const bool b : {false, true})
        {
            QShortcut *shortcut = new QShortcut(QKeySequence(b ? Qt::Key_BracketRight : Qt::Key_BracketLeft), window());
            connect(shortcut, &QShortcut::activated, this, [this, b]() { m_strip->setMark(b, m_strip->position()); });
        }
    m_cutButton = button(tr("Cut A–B"), tr("Leave the part between A and B out of the export (Delete)"));
    connect(m_cutButton, &QToolButton::clicked, this, [this]()
    {
        const TimeRange part = m_strip->marked();
        if (part.length() > 0.0)
            {
                emit cutsChanged(CutList::added(m_cuts, part), tr("Cut part"));
                m_strip->setMark(false, -1.0);
                m_strip->setMark(true, -1.0);
            }
    });
    QShortcut *cutShortcut = new QShortcut(QKeySequence::Delete, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(cutShortcut, &QShortcut::activated, m_cutButton, &QToolButton::click);
    m_keepButton = button(tr("Keep A–B"), tr("Put the part between A and B back"));
    connect(m_keepButton, &QToolButton::clicked, this, [this]()
    {
        const TimeRange part = m_strip->marked();
        if (part.length() > 0.0)
            {
                emit cutsChanged(CutList::removed(m_cuts, part), tr("Keep part again"));
            }
    });
    m_stopsButton = button(tr("Cut stops"), tr("Leave out every stop of 10 s or more, keeping 2 s at each end"));
    connect(m_stopsButton, &QToolButton::clicked, this, &TimelinePanel::findStops);
    m_clearButton = button(tr("Keep all"), tr("Remove all cuts"));
    connect(m_clearButton, &QToolButton::clicked, this, [this]() { emit cutsChanged({}, tr("Remove all cuts")); });
    m_keptLabel = new QLabel(this);

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Show:"), this));
    top->addWidget(m_channelCombo);
    top->addSpacing(12);
    for (QToolButton *b : {markA, markB, m_cutButton, m_keepButton, m_stopsButton, m_clearButton})
        {
            top->addWidget(b);
        }
    top->addSpacing(12);
    top->addWidget(m_keptLabel, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addLayout(top);
    layout->addWidget(m_strip, 1);
    updateCutButtons();
}

void TimelinePanel::setSession(const TelemetrySessionPtr &session, const Units &units)
{
    m_session = session;
    m_units = units;
    m_strip->setSession(session);
    fillChannels();
    updateCutButtons();
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
    m_videoDuration = seconds;
    m_strip->setVideoDuration(seconds);
    updateCutButtons();
}

void TimelinePanel::setSync(const SyncMap &sync)
{
    m_sync = sync;
    m_strip->setSync(sync);
}

void TimelinePanel::setCuts(const QVector<TimeRange> &cuts)
{
    m_cuts = cuts;
    m_strip->setCuts(cuts);
    updateCutButtons();
}

void TimelinePanel::updateCutButtons()
{
    const bool marked = m_strip->marked().length() > 0.0;
    m_cutButton->setEnabled(marked);
    m_keepButton->setEnabled(marked && !m_cuts.isEmpty());
    m_clearButton->setEnabled(!m_cuts.isEmpty());
    m_stopsButton->setEnabled(m_session && m_videoDuration > 0.0);
    if (m_cuts.isEmpty() || m_videoDuration <= 0.0)
        {
            m_keptLabel->clear();
            return;
        }
    const double kept = CutList::keptLength(CutList::kept(m_cuts, 0.0, m_videoDuration));
    m_keptLabel->setText(tr("%n part(s) cut, %1 of %2 kept", nullptr, int(m_cuts.size()))
                         .arg(clock(kept), clock(m_videoDuration)));
}

void TimelinePanel::findStops()
{
    if (!m_session)
        {
            return;
        }
    const QVector<TimeRange> stops = CutList::stops(*m_session, m_sync, m_videoDuration);
    if (stops.isEmpty())
        {
            m_keptLabel->setText(tr("No stops of 10 s or more in the video."));
            return;
        }
    QVector<TimeRange> cuts = m_cuts;
    for (const TimeRange &stop : stops)
        {
            cuts = CutList::added(cuts, stop);
        }
    emit cutsChanged(cuts, tr("Cut stops"));
}

void TimelinePanel::setPosition(double videoSeconds)
{
    m_strip->setPosition(videoSeconds);
}
