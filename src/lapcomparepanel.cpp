#include "lapcomparepanel.h"

#include "laps/lapengine.h"
#include "chartsmoothing.h"
#include "telemetry/channelids.h"
#include "telemetry/smoothing.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace
{
const int kLeft = 52;
const int kRight = 8;
const int kLegend = 20;
const int kAxis = 18;
const int kGap = 10;

QString lapTime(double seconds)
{
    const int minutes = int(seconds / 60.0);
    return QString("%1:%2").arg(minutes).arg(seconds - minutes * 60.0, 6, 'f', 3, QLatin1Char('0'));
}

// a round step of about `rough`
double niceStep(double rough)
{
    const double power = std::pow(10.0, std::floor(std::log10(std::max(rough, 1e-9))));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        {
            if (m * power >= rough)
                {
                    return m * power;
                }
        }
    return 10.0 * power;
}
}

// The drawing: values above, delta below, both against distance into the lap.
class LapCompareChart : public QWidget
{
public:
    struct Series
    {
        QString label;
        QColor colour;
        QVector<QPointF> values;   // distance (m), shown value
        QVector<QPointF> delta;    // distance (m), s behind the reference
    };

    explicit LapCompareChart(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setSeries(const QVector<Series> &series, const ChannelDisplay &value, const ChannelDisplay &distance,
                   const QString &empty)
    {
        m_series = series;
        m_value = value;
        m_distance = distance;
        m_empty = empty;
        m_length = 0.0;
        m_low = std::numeric_limits<double>::max();
        m_high = std::numeric_limits<double>::lowest();
        m_deltaRange = 0.5;
        for (const Series &s : m_series)
            {
                for (const QPointF &p : s.values)
                    {
                        m_length = std::max(m_length, p.x());
                        m_low = std::min(m_low, p.y());
                        m_high = std::max(m_high, p.y());
                    }
                for (const QPointF &p : s.delta)
                    {
                        m_length = std::max(m_length, p.x());
                        m_deltaRange = std::max(m_deltaRange, std::fabs(p.y()));
                    }
            }
        if (m_high <= m_low)
            {
                m_low -= 1.0;
                m_high += 1.0;
            }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        if (m_series.size() < 2 || m_length <= 0.0)
            {
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_empty);
                return;
            }
        // legend
        int x = kLeft;
        for (const Series &s : m_series)
            {
                painter.fillRect(QRect(x, 6, 14, 8), s.colour);
                x += 18;
                painter.setPen(palette().color(QPalette::Text));
                painter.drawText(QPoint(x, 14), s.label);
                x += fontMetrics().horizontalAdvance(s.label) + 14;
            }
        const QRect values = valueRect();
        const QRect delta = deltaRect();
        drawGrid(painter, values, delta);
        painter.setRenderHint(QPainter::Antialiasing);
        for (const Series &s : m_series)
            {
                painter.setPen(QPen(s.colour, 1.5));
                painter.drawPath(path(s.values, values, m_low, m_high));
                painter.drawPath(path(s.delta, delta, -m_deltaRange, m_deltaRange));
            }
        painter.setRenderHint(QPainter::Antialiasing, false);
        if (m_hoverX >= values.left() && m_hoverX <= values.right())
            {
                drawHover(painter, values, delta);
            }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        m_hoverX = int(event->position().x());
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hoverX = -1;
        update();
    }

private:
    QRect valueRect() const
    {
        const int plots = height() - kLegend - kAxis - kGap - 4;
        return QRect(kLeft, kLegend, width() - kLeft - kRight, std::max(20, plots * 62 / 100));
    }

    QRect deltaRect() const
    {
        const QRect values = valueRect();
        return QRect(kLeft, values.bottom() + kGap, values.width(), std::max(16, height() - values.bottom() - kGap - kAxis));
    }

    double xOf(double metres, const QRect &area) const { return area.left() + metres / m_length * area.width(); }

    QPainterPath path(const QVector<QPointF> &points, const QRect &area, double low, double high) const
    {
        QPainterPath line;
        for (int i = 0; i < points.size(); ++i)
            {
                const QPointF p(xOf(points.at(i).x(), area),
                                area.bottom() - (points.at(i).y() - low) / (high - low) * area.height());
                if (i == 0)
                    {
                        line.moveTo(p);
                    }
                else
                    {
                        line.lineTo(p);
                    }
            }
        return line;
    }

    void drawGrid(QPainter &painter, const QRect &values, const QRect &delta)
    {
        QColor grid = palette().color(QPalette::Text);
        grid.setAlpha(40);
        const QColor label = palette().color(QPalette::PlaceholderText);
        // distance along the bottom
        const double shownLength = m_distance.convert(m_length);
        const double step = niceStep(shownLength / std::max(2.0, values.width() / 90.0));
        for (double d = 0.0; d <= shownLength + 1e-9; d += step)
            {
                const double x = xOf((d - m_distance.offset) / m_distance.scale, values);
                painter.setPen(grid);
                painter.drawLine(QPointF(x, values.top()), QPointF(x, delta.bottom()));
                painter.setPen(label);
                painter.drawText(QPointF(x + 2, delta.bottom() + kAxis - 4),
                                 QString::number(d, 'f', step < 1.0 ? 1 : 0) + (d == 0.0 ? QString(" %1").arg(m_distance.unit) : QString()));
            }
        // values at the side
        const double valueStep = niceStep((m_high - m_low) / std::max(2.0, values.height() / 30.0));
        for (double v = std::ceil(m_low / valueStep) * valueStep; v <= m_high; v += valueStep)
            {
                const double y = values.bottom() - (v - m_low) / (m_high - m_low) * values.height();
                painter.setPen(grid);
                painter.drawLine(QPointF(values.left(), y), QPointF(values.right(), y));
                painter.setPen(label);
                painter.drawText(QRectF(0, y - 8, kLeft - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(v, 'f', valueStep < 1.0 ? 1 : 0));
            }
        painter.setPen(label);
        painter.drawText(QRect(values.left() + 4, values.top(), values.width(), 16), Qt::AlignLeft | Qt::AlignTop,
                         QString("%1 %2").arg(m_value.name, m_value.unit).trimmed());
        // delta: zero, and how far either way
        const double zero = delta.center().y();
        painter.setPen(grid);
        painter.drawLine(QPointF(delta.left(), zero), QPointF(delta.right(), zero));
        painter.drawRect(delta.adjusted(0, 0, -1, -1));
        painter.setPen(label);
        const int decimals = m_deltaRange < 10.0 ? 1 : 0;
        painter.drawText(QRectF(0, delta.top() - 2, kLeft - 4, 16), Qt::AlignRight | Qt::AlignTop,
                         QString("+%1").arg(m_deltaRange, 0, 'f', decimals));
        painter.drawText(QRectF(0, delta.bottom() - 14, kLeft - 4, 16), Qt::AlignRight | Qt::AlignBottom,
                         QString("-%1").arg(m_deltaRange, 0, 'f', decimals));
        painter.drawText(QRect(delta.left() + 4, delta.top() + 1, delta.width(), 16), Qt::AlignLeft | Qt::AlignTop,
                         LapComparePanel::tr("Time behind the fastest, s"));
    }

    static double valueAtDistance(const QVector<QPointF> &points, double metres, bool *ok)
    {
        const auto next = std::lower_bound(points.cbegin(), points.cend(), metres,
                                           [](const QPointF &p, double m) { return p.x() < m; });
        *ok = next != points.cend();
        if (!*ok)
            {
                return 0.0;
            }
        if (next == points.cbegin() || next->x() == metres)
            {
                return next->y();
            }
        const QPointF a = *(next - 1);
        const QPointF b = *next;
        return a.y() + (b.y() - a.y()) * (metres - a.x()) / (b.x() - a.x());
    }

    void drawHover(QPainter &painter, const QRect &values, const QRect &delta)
    {
        const double metres = (m_hoverX - values.left()) / double(values.width()) * m_length;
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
        painter.drawLine(m_hoverX, values.top(), m_hoverX, delta.bottom());
        QStringList lines{QString("%1 %2").arg(m_distance.convert(metres), 0, 'f', m_distance.decimals).arg(m_distance.unit)};
        for (const Series &s : m_series)
            {
                bool ok = false;
                bool deltaOk = false;
                const double value = valueAtDistance(s.values, metres, &ok);
                const double behind = valueAtDistance(s.delta, metres, &deltaOk);
                lines << QString("%1: %2  %3").arg(s.label.section(' ', 0, 1),
                                                   ok ? QString::number(value, 'f', m_value.decimals) : QStringLiteral("--"),
                                                   deltaOk ? QString("%1%2").arg(behind >= 0.0 ? "+" : "").arg(behind, 0, 'f', 2)
                                                   : QString());
            }
        int w = 0;
        for (const QString &line : lines)
            {
                w = std::max(w, fontMetrics().horizontalAdvance(line));
            }
        const int h = fontMetrics().height() * int(lines.size()) + 6;
        QRect box(m_hoverX + 8, values.top() + 18, w + 10, h);
        if (box.right() > width())
            {
                box.moveRight(m_hoverX - 8);
            }
        QColor back = palette().color(QPalette::ToolTipBase);
        back.setAlpha(230);
        painter.fillRect(box, back);
        painter.setPen(palette().color(QPalette::ToolTipText));
        painter.drawText(box.adjusted(5, 3, -5, -3), Qt::AlignLeft | Qt::AlignTop, lines.join('\n'));
    }

    QVector<Series> m_series;
    ChannelDisplay m_value;
    ChannelDisplay m_distance;
    QString m_empty;
    double m_length{0.0};
    double m_low{0.0};
    double m_high{1.0};
    double m_deltaRange{0.5};
    int m_hoverX{-1};
};

LapComparePanel::LapComparePanel(QWidget *parent)
    : QWidget(parent)
{
    m_chart = new LapCompareChart(this);
    m_channelCombo = new QComboBox(this);
    m_channelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_channelCombo->setMinimumContentsLength(10);
    m_channelCombo->setToolTip(tr("The measurement compared"));
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::activated), this, &LapComparePanel::rebuild);
    m_smoothingCombo = ChartSmoothing::makeCombo(this);
    m_smoothing = ChartSmoothing::saved();
    connect(m_smoothingCombo, QOverload<int>::of(&QComboBox::activated), this, [this]()
    {
        setSmoothing(m_smoothingCombo->currentData().toDouble());
        ChartSmoothing::save(m_smoothing);
        emit smoothingChanged(m_smoothing);
    });
    QLabel *hint = new QLabel(tr("Select laps in the Laps panel with Ctrl+click or Shift+click"), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    // the panel may be narrower than the hint: it is cut, not the window widened
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hint->setToolTip(hint->text());

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Show:"), this));
    top->addWidget(m_channelCombo);
    top->addWidget(m_smoothingCombo);
    top->addSpacing(12);
    top->addWidget(hint, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addLayout(top);
    layout->addWidget(m_chart, 1);
    rebuild();
}

void LapComparePanel::setSession(const TelemetrySessionPtr &session, const Units &units)
{
    m_session = session;
    m_units = units;
    // laps may be others now
    m_laps.clear();
    fillChannels();
    rebuild();
}

void LapComparePanel::setSmoothing(double seconds)
{
    m_smoothing = seconds;
    ChartSmoothing::select(m_smoothingCombo, seconds);
    rebuild();
}

void LapComparePanel::setLaps(const QVector<int> &laps)
{
    m_laps = laps;
    rebuild();
}

void LapComparePanel::fillChannels()
{
    const QString current = m_channelCombo->currentData().toString();
    m_channelCombo->clear();
    if (m_session)
        {
            for (const QString &id : m_session->channelIds())
                {
                    const Channel *channel = m_session->channel(id);
                    // what changes along a lap; lap bookkeeping and positions do not compare
                    if (channel && !channel->isEmpty() && id != ChannelId::PositionLat && id != ChannelId::PositionLon
                            && id != ChannelId::Distance && !id.startsWith("lap") && !id.startsWith("sector")
                            && !id.endsWith("LapTime") && !id.endsWith("SectorTime"))
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
}

void LapComparePanel::rebuild()
{
    static const QColor colours[] = {QColor(150, 60, 200), QColor(30, 140, 230), QColor(230, 120, 20),
                                     QColor(40, 170, 90), QColor(220, 50, 60), QColor(140, 140, 30)};
    QVector<LapCompareChart::Series> series;
    const QString id = m_channelCombo->currentData().toString();
    const Channel *channel = m_session ? m_session->channel(id) : nullptr;
    const ChannelDisplay value = channel ? ChannelFormat::displayFor(*channel, m_units) : ChannelDisplay();
    const ChannelDisplay distance = ChannelFormat::displayFor(ChannelId::Distance, "m", m_units);
    QString empty = tr("Select two or more laps in the Laps panel to compare them.");
    if (m_session && !m_session->channel(ChannelId::Distance))
        {
            empty = tr("Laps are compared by distance, and the data has none.");
        }
    const QVector<TelemetrySession::Lap> laps = m_session ? m_session->laps() : QVector<TelemetrySession::Lap>();
    QVector<int> chosen;
    for (const int i : m_laps)
        {
            if (i >= 0 && i < laps.size() && laps.at(i).endTime > laps.at(i).startTime)
                {
                    chosen.append(i);
                }
        }
    // smoothed: the same session with that one channel evened out
    TelemetrySessionPtr source = m_session;
    if (chosen.size() >= 2 && channel && m_smoothing > 0.0)
        {
            const Channel smooth = Smoothing::movingAverage(*channel, m_smoothing);
            auto copy = std::make_shared<TelemetrySession>(*m_session);
            copy->removeChannel(id);
            Channel &replaced = copy->channelForWriting(id, smooth.unit(), smooth.interpolation());
            for (int i = 0; i < smooth.count(); ++i)
                {
                    replaced.append(smooth.timeAt(i), smooth.valueAtIndex(i));
                }
            source = copy;
        }
    if (chosen.size() >= 2 && channel)
        {
            // the fastest is what the others are measured against
            int reference = chosen.first();
            for (const int i : chosen)
                {
                    if (laps.at(i).endTime - laps.at(i).startTime < laps.at(reference).endTime - laps.at(reference).startTime)
                        {
                            reference = i;
                        }
                }
            for (int k = 0; k < chosen.size(); ++k)
                {
                    const TelemetrySession::Lap &lap = laps.at(chosen.at(k));
                    LapCompareChart::Series s;
                    s.label = tr("Lap %1 (%2)").arg(chosen.at(k) + 1).arg(lapTime(lap.endTime - lap.startTime));
                    s.colour = colours[k % 6];
                    for (const QPointF &p : LapEngine::byDistance(*source, lap, id))
                        {
                            s.values.append(QPointF(p.x(), value.convert(p.y())));
                        }
                    s.delta = LapEngine::deltaByDistance(*m_session, laps.at(reference), lap);
                    series.append(s);
                }
        }
    m_chart->setSeries(series, value, distance, empty);
}
