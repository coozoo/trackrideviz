#include "datapanel.h"

#include "telemetry/channelids.h"

#include <QAbstractTableModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
const int kLaneGap = 4;
const int kAxis = 16;

QString clock(double seconds, bool fraction = false)
{
    const double whole = std::floor(seconds);
    const qint64 s = qint64(whole);
    QString text = s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                   : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    if (fraction)
        {
            text += QString(".%1").arg(int(std::lround((seconds - whole) * 1000.0)) % 1000, 3, 10, QLatin1Char('0'));
        }
    return text;
}

bool edited(const QVector<DataEdit> &edits, const QString &channel, double t)
{
    const bool position = channel == ChannelId::PositionLat || channel == ChannelId::PositionLon;
    for (const DataEdit &edit : edits)
        {
            const bool same = edit.channel == channel
                              || (position && (edit.channel == ChannelId::PositionLat || edit.channel == ChannelId::PositionLon));
            if (same && t >= edit.start && t <= edit.end)
                {
                    return true;
                }
        }
    return false;
}
}

// The samples of one channel as recorded, those taken out shown struck through.
class DataTableModel : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    void setChannel(const Channel *channel, const ChannelDisplay &display, const QVector<DataEdit> &edits)
    {
        beginResetModel();
        m_channel = channel;
        m_display = display;
        m_edits = edits;
        endResetModel();
    }

    const Channel *channel() const { return m_channel; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() || !m_channel ? 0 : m_channel->count();
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : 2; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!m_channel || !index.isValid())
            {
                return QVariant();
            }
        const double t = m_channel->timeAt(index.row());
        if (role == Qt::DisplayRole)
            {
                return index.column() == 0 ? clock(t, true)
                       : QVariant(QString::number(m_display.convert(m_channel->valueAtIndex(index.row())), 'f',
                                                  std::max(m_display.decimals, 1)));
            }
        if (role == Qt::TextAlignmentRole)
            {
                return int(Qt::AlignRight | Qt::AlignVCenter);
            }
        if (role == Qt::FontRole && edited(m_edits, m_channel->id(), t))
            {
                QFont font;
                font.setStrikeOut(true);
                return font;
            }
        if (role == Qt::ToolTipRole && edited(m_edits, m_channel->id(), t))
            {
                return DataPanel::tr("Taken out");
            }
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            {
                return QAbstractTableModel::headerData(section, orientation, role);
            }
        return section == 0 ? DataPanel::tr("Time") : QString("%1 %2").arg(m_display.name, m_display.unit).trimmed();
    }

private:
    const Channel *m_channel{nullptr};
    ChannelDisplay m_display;
    QVector<DataEdit> m_edits;
};

// Lanes, one per channel, on one time axis.
class DataChart : public QWidget
{
public:
    struct Lane
    {
        QString id;
        ChannelDisplay display;
        const Channel *shown{nullptr};     // corrected
        const Channel *recorded{nullptr};  // as recorded, drawn faintly under it; may be null
        double low{0.0};
        double high{1.0};
    };

    explicit DataChart(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    std::function<void(double)> seek;
    std::function<void(const QString &)> laneClicked;
    std::function<void()> selectionChanged;

    void setLanes(const QVector<Lane> &lanes, double duration, const QVector<DataEdit> &edits)
    {
        m_lanes = lanes;
        m_edits = edits;
        if (duration != m_duration)
            {
                m_duration = duration;
                m_viewStart = 0.0;
                m_viewEnd = duration;
            }
        if (m_selection.isEmpty() == false)
            {
                bool still = false;
                for (const Lane &lane : m_lanes)
                    {
                        still = still || lane.id == m_selection;
                    }
                if (!still)
                    {
                        clearSelection();
                    }
            }
        update();
    }

    void setTime(double t)
    {
        m_time = t;
        update();
    }

    void clearSelection()
    {
        m_selection.clear();
        m_selectFrom = m_selectTo = 0.0;
        update();
        if (selectionChanged)
            {
                selectionChanged();
            }
    }

    bool selected(QString *channel, double *start, double *end) const
    {
        if (m_selection.isEmpty() || m_selectTo == m_selectFrom)
            {
                return false;
            }
        *channel = m_selection;
        *start = std::min(m_selectFrom, m_selectTo);
        *end = std::max(m_selectFrom, m_selectTo);
        return true;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        if (m_lanes.isEmpty() || m_viewEnd <= m_viewStart)
            {
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, DataPanel::tr("Open data and tick channels to see them"));
                return;
            }
        drawAxis(painter);
        for (int i = 0; i < m_lanes.size(); ++i)
            {
                drawLane(painter, m_lanes.at(i), laneRect(i));
            }
        const double x = xOf(m_time);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawLine(QPointF(x, 0), QPointF(x, height()));
        if (m_hoverX >= 0)
            {
                drawHover(painter);
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_lanes.isEmpty())
            {
                return;
            }
        const int lane = laneAt(event->position().y());
        const double t = std::clamp(timeOf(event->position().x()), 0.0, m_duration);
        if (lane >= 0 && laneClicked)
            {
                laneClicked(m_lanes.at(lane).id);
            }
        if ((event->modifiers() & Qt::ShiftModifier) && lane >= 0)
            {
                m_selecting = true;
                m_selection = m_lanes.at(lane).id;
                m_selectFrom = m_selectTo = t;
                update();
                return;
            }
        if (seek)
            {
                seek(t);
            }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        m_hoverX = int(event->position().x());
        if (m_selecting)
            {
                m_selectTo = std::clamp(timeOf(event->position().x()), 0.0, m_duration);
            }
        else if ((event->buttons() & Qt::LeftButton) && seek)
            {
                seek(std::clamp(timeOf(event->position().x()), 0.0, m_duration));
            }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (m_selecting)
            {
                m_selecting = false;
                if (selectionChanged)
                    {
                        selectionChanged();
                    }
            }
    }

    void leaveEvent(QEvent *) override
    {
        m_hoverX = -1;
        update();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_duration <= 0.0)
            {
                return;
            }
        const double steps = event->angleDelta().y() / 120.0;
        const double span = m_viewEnd - m_viewStart;
        if (event->modifiers() & Qt::ShiftModifier || event->angleDelta().x() != 0)
            {
                const double pan = (event->angleDelta().x() != 0 ? event->angleDelta().x() / 120.0 : steps) * span * 0.1;
                setView(m_viewStart - pan, span);
            }
        else
            {
                const double at = timeOf(event->position().x());
                const double newSpan = std::clamp(span * std::pow(0.8, steps), 2.0, m_duration);
                setView(at - (at - m_viewStart) / span * newSpan, newSpan);
            }
        event->accept();
    }

private:
    double pixelsPerSecond() const { return width() / std::max(m_viewEnd - m_viewStart, 1e-6); }
    double xOf(double t) const { return (t - m_viewStart) * pixelsPerSecond(); }
    double timeOf(double x) const { return m_viewStart + x / pixelsPerSecond(); }

    void setView(double start, double span)
    {
        span = std::min(span, m_duration);
        m_viewStart = std::clamp(start, 0.0, m_duration - span);
        m_viewEnd = m_viewStart + span;
        update();
    }

    QRect laneRect(int i) const
    {
        const int count = int(m_lanes.size());
        const int h = (height() - kAxis - kLaneGap * count) / std::max(1, count);
        return QRect(0, kAxis + i * (h + kLaneGap), width(), std::max(10, h));
    }

    int laneAt(double y) const
    {
        for (int i = 0; i < m_lanes.size(); ++i)
            {
                if (laneRect(i).contains(QPoint(0, int(y))))
                    {
                        return i;
                    }
            }
        return -1;
    }

    void drawAxis(QPainter &painter)
    {
        const double minimum = 80.0 / pixelsPerSecond();
        double step = 1.0;
        for (const double s : {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0})
            {
                step = s;
                if (s >= minimum)
                    {
                        break;
                    }
            }
        QColor grid = palette().color(QPalette::Text);
        grid.setAlpha(35);
        for (double t = std::ceil(m_viewStart / step) * step; t <= m_viewEnd; t += step)
            {
                const double x = xOf(t);
                painter.setPen(grid);
                painter.drawLine(QPointF(x, kAxis - 3), QPointF(x, height()));
                painter.setPen(palette().color(QPalette::PlaceholderText));
                painter.drawText(QPointF(x + 2, kAxis - 4), clock(t, step < 1.0));
            }
    }

    // min and max of each pixel column, so a single spike is never lost
    void drawTrace(QPainter &painter, const Channel *channel, const Lane &lane, const QRect &area, const QPen &pen)
    {
        if (!channel || channel->isEmpty())
            {
                return;
            }
        auto yOf = [&](double v)
        {
            return area.bottom() - (lane.display.convert(v) - lane.low) / (lane.high - lane.low) * (area.height() - 2) - 1;
        };
        QPainterPath path;
        bool started = false;
        int index = 0;
        const int count = channel->count();
        const double secondsPerPixel = 1.0 / pixelsPerSecond();
        // few samples on screen: straight lines between them
        int first = 0;
        while (first < count && channel->timeAt(first) < m_viewStart)
            {
                ++first;
            }
        int onScreen = 0;
        for (int i = first; i < count && channel->timeAt(i) <= m_viewEnd; ++i)
            {
                ++onScreen;
            }
        if (onScreen < width() / 2)
            {
                for (int i = std::max(0, first - 1); i < count; ++i)
                    {
                        const QPointF p(xOf(channel->timeAt(i)), yOf(channel->valueAtIndex(i)));
                        started ? path.lineTo(p) : path.moveTo(p);
                        started = true;
                        if (channel->timeAt(i) > m_viewEnd)
                            {
                                break;
                            }
                    }
            }
        else
            {
                index = first;
                for (int x = 0; x < width() && index < count; ++x)
                    {
                        const double until = m_viewStart + (x + 1) * secondsPerPixel;
                        double low = std::numeric_limits<double>::max();
                        double high = std::numeric_limits<double>::lowest();
                        while (index < count && channel->timeAt(index) < until)
                            {
                                low = std::min(low, channel->valueAtIndex(index));
                                high = std::max(high, channel->valueAtIndex(index));
                                ++index;
                            }
                        if (low > high)
                            {
                                continue;
                            }
                        const QPointF a(x, yOf(low));
                        started ? path.lineTo(a) : path.moveTo(a);
                        started = true;
                        if (high != low)
                            {
                                path.lineTo(QPointF(x, yOf(high)));
                            }
                    }
            }
        painter.setPen(pen);
        painter.drawPath(path);
    }

    void drawLane(QPainter &painter, const Lane &lane, const QRect &area)
    {
        QColor back = palette().color(QPalette::AlternateBase);
        painter.fillRect(area, back);
        // what is taken out
        for (const DataEdit &edit : m_edits)
            {
                if (edited({edit}, lane.id, (edit.start + edit.end) / 2.0))
                    {
                        const QRectF band(QPointF(xOf(edit.start), area.top()), QPointF(std::max(xOf(edit.end), xOf(edit.start) + 2), area.bottom()));
                        painter.fillRect(band, QColor(220, 40, 40, 50));
                    }
            }
        if (lane.id == m_selection && m_selectTo != m_selectFrom)
            {
                QColor fill = palette().color(QPalette::Highlight);
                fill.setAlpha(70);
                painter.fillRect(QRectF(QPointF(xOf(std::min(m_selectFrom, m_selectTo)), area.top()),
                                        QPointF(xOf(std::max(m_selectFrom, m_selectTo)), area.bottom())), fill);
            }
        painter.save();
        painter.setClipRect(area);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor faint = palette().color(QPalette::Text);
        faint.setAlpha(70);
        if (lane.recorded && !m_edits.isEmpty())
            {
                drawTrace(painter, lane.recorded, lane, area, QPen(faint, 1.0, Qt::DashLine));
            }
        drawTrace(painter, lane.shown, lane, area, QPen(palette().color(QPalette::Text), 1.2));
        painter.restore();
        painter.setPen(palette().color(QPalette::PlaceholderText));
        const QString title = QString("%1 %2").arg(lane.display.name, lane.display.unit).trimmed();
        painter.drawText(area.adjusted(4, 1, -4, -1), Qt::AlignLeft | Qt::AlignTop, title);
        painter.drawText(area.adjusted(4, 1, -4, -1), Qt::AlignRight | Qt::AlignTop,
                         QString::number(lane.high, 'f', lane.display.decimals));
        painter.drawText(area.adjusted(4, 1, -4, -1), Qt::AlignRight | Qt::AlignBottom,
                         QString::number(lane.low, 'f', lane.display.decimals));
    }

    void drawHover(QPainter &painter)
    {
        const double t = timeOf(m_hoverX);
        painter.setPen(QPen(palette().color(QPalette::Text), 1, Qt::DotLine));
        painter.drawLine(m_hoverX, kAxis, m_hoverX, height());
        for (int i = 0; i < m_lanes.size(); ++i)
            {
                const Lane &lane = m_lanes.at(i);
                bool ok = false;
                const double v = lane.shown ? lane.shown->valueAt(t, &ok) : 0.0;
                const QString text = QString("%1  %2").arg(clock(t, true),
                                                           ok ? QString::number(lane.display.convert(v), 'f', lane.display.decimals) : QStringLiteral("--"));
                const QRect area = laneRect(i);
                const int w = fontMetrics().horizontalAdvance(text) + 8;
                QRect box(m_hoverX + 6, area.top() + area.height() / 2 - 8, w, 16);
                if (box.right() > width())
                    {
                        box.moveRight(m_hoverX - 6);
                    }
                QColor fill = palette().color(QPalette::ToolTipBase);
                fill.setAlpha(220);
                painter.fillRect(box, fill);
                painter.setPen(palette().color(QPalette::ToolTipText));
                painter.drawText(box, Qt::AlignCenter, text);
            }
    }

    QVector<Lane> m_lanes;
    QVector<DataEdit> m_edits;
    double m_duration{0.0};
    double m_viewStart{0.0};
    double m_viewEnd{0.0};
    double m_time{0.0};
    int m_hoverX{-1};
    bool m_selecting{false};
    QString m_selection;     // the lane's channel
    double m_selectFrom{0.0};
    double m_selectTo{0.0};
};

DataPanel::DataPanel(QWidget *parent)
    : QWidget(parent)
{
    m_channelList = new QListWidget(this);
    m_channelList->setToolTip(tr("Tick the channels to chart"));
    connect(m_channelList, &QListWidget::itemChanged, this, &DataPanel::updateCharts);
    connect(m_channelList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item)
    {
        if (item)
            {
                showTableOf(item->data(Qt::UserRole).toString());
            }
    });

    m_chart = new DataChart(this);
    m_chart->setToolTip(tr("Click to go there, wheel to zoom (Shift+wheel: move).\n"
                           "Shift+drag over a channel picks samples to take out."));
    m_chart->seek = [this](double t) { emit seekRequested(t); };
    m_chart->laneClicked = [this](const QString &id) { showTableOf(id); };
    m_chart->selectionChanged = [this]() { updateButtons(); };

    m_model = new DataTableModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 4);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setToolTip(tr("Double-click a sample to go there; select samples to take them out"));
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &index)
    {
        if (m_model->channel())
            {
                emit seekRequested(m_model->channel()->timeAt(index.row()));
            }
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]()
    {
        if (m_table->hasFocus())
            {
                m_chart->clearSelection();
            }
        updateButtons();
    });
    m_table->hide();

    auto button = [this](const QString &text, const QString &tip)
    {
        QToolButton *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    m_removeButton = button(tr("Take out"), tr("Leave the picked samples out; the channel is read across the gap"));
    connect(m_removeButton, &QToolButton::clicked, this, &DataPanel::removeSelected);
    m_restoreButton = button(tr("Put back"), tr("Put back samples taken out in the picked stretch"));
    connect(m_restoreButton, &QToolButton::clicked, this, &DataPanel::restoreSelected);
    m_tableButton = button(tr("Table"), tr("Show the samples of the channel last clicked"));
    m_tableButton->setCheckable(true);
    connect(m_tableButton, &QToolButton::toggled, m_table, &QTableView::setVisible);

    QHBoxLayout *tools = new QHBoxLayout;
    tools->addWidget(m_removeButton);
    tools->addWidget(m_restoreButton);
    tools->addStretch(1);
    tools->addWidget(m_tableButton);

    QWidget *right = new QWidget(this);
    QHBoxLayout *chartAndTable = new QHBoxLayout;
    chartAndTable->addWidget(m_chart, 3);
    chartAndTable->addWidget(m_table, 1);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addLayout(tools);
    rightLayout->addLayout(chartAndTable, 1);

    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(m_channelList);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({180, 800});
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->addWidget(splitter);
    updateButtons();
}

void DataPanel::setSessions(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session, const Units &units)
{
    const bool other = session.get() == nullptr || m_session.get() == nullptr
                       || session->channelIds() != m_session->channelIds();
    m_raw = raw;
    m_session = session;
    m_units = units;
    if (other)
        {
            fillChannels();
        }
    updateCharts();
    showTableOf(m_tableChannel);
}

void DataPanel::setEdits(const QVector<DataEdit> &edits)
{
    m_edits = edits;
    updateCharts();
    showTableOf(m_tableChannel);
}

void DataPanel::setTime(double time)
{
    m_chart->setTime(time);
}

void DataPanel::fillChannels()
{
    QStringList ticked;
    for (int i = 0; i < m_channelList->count(); ++i)
        {
            if (m_channelList->item(i)->checkState() == Qt::Checked)
                {
                    ticked << m_channelList->item(i)->data(Qt::UserRole).toString();
                }
        }
    if (ticked.isEmpty())
        {
            ticked = {ChannelId::Speed, ChannelId::Elevation, ChannelId::HeartRate};
        }
    const QSignalBlocker blocker(m_channelList);
    m_channelList->clear();
    if (!m_session)
        {
            return;
        }
    for (const QString &id : m_session->channelIds())
        {
            const Channel *channel = m_session->channel(id);
            if (!channel || channel->isEmpty())
                {
                    continue;
                }
            QListWidgetItem *item = new QListWidgetItem(ChannelFormat::displayFor(*channel, m_units).name, m_channelList);
            item->setData(Qt::UserRole, id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(ticked.contains(id) ? Qt::Checked : Qt::Unchecked);
            if (m_session->derivedChannels().contains(id))
                {
                    QFont font = item->font();
                    font.setItalic(true);
                    item->setFont(font);
                    item->setToolTip(tr("Worked out from the other data"));
                }
        }
}

void DataPanel::updateCharts()
{
    QVector<DataChart::Lane> lanes;
    for (int i = 0; m_session && i < m_channelList->count(); ++i)
        {
            if (m_channelList->item(i)->checkState() != Qt::Checked)
                {
                    continue;
                }
            DataChart::Lane lane;
            lane.id = m_channelList->item(i)->data(Qt::UserRole).toString();
            lane.shown = m_session->channel(lane.id);
            lane.recorded = m_raw ? m_raw->channel(lane.id) : nullptr;
            if (!lane.shown)
                {
                    continue;
                }
            lane.display = ChannelFormat::displayFor(*lane.shown, m_units);
            // the scale of what is shown: a taken-out spike should not squash the rest
            const double a = lane.display.convert(lane.shown->minValue());
            const double b = lane.display.convert(lane.shown->maxValue());
            lane.low = std::min(a, b);
            lane.high = std::max(a, b);
            if (lane.high <= lane.low)
                {
                    lane.low -= 1.0;
                    lane.high += 1.0;
                }
            lanes.append(lane);
        }
    m_chart->setLanes(lanes, m_session ? m_session->duration() : 0.0, m_edits);
    if (m_tableChannel.isEmpty() && !lanes.isEmpty())
        {
            showTableOf(lanes.first().id);
        }
    updateButtons();
}

void DataPanel::showTableOf(const QString &channelId)
{
    m_tableChannel = channelId;
    // the samples as recorded, so those taken out can be seen and put back
    const Channel *channel = m_raw && m_raw->channel(channelId) ? m_raw->channel(channelId)
                             : m_session ? m_session->channel(channelId) : nullptr;
    m_model->setChannel(channel, channel ? ChannelFormat::displayFor(*channel, m_units) : ChannelDisplay(), m_edits);
    updateButtons();
}

bool DataPanel::selection(QString *channel, double *start, double *end) const
{
    if (m_chart->selected(channel, start, end))
        {
            return true;
        }
    const QModelIndexList rows = m_table->isVisible() && m_model->channel()
                                 ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty())
        {
            return false;
        }
    *channel = m_model->channel()->id();
    *start = std::numeric_limits<double>::max();
    *end = std::numeric_limits<double>::lowest();
    for (const QModelIndex &row : rows)
        {
            *start = std::min(*start, m_model->channel()->timeAt(row.row()));
            *end = std::max(*end, m_model->channel()->timeAt(row.row()));
        }
    return true;
}

void DataPanel::updateButtons()
{
    QString channel;
    double start = 0.0;
    double end = 0.0;
    const bool picked = selection(&channel, &start, &end);
    m_removeButton->setEnabled(picked);
    m_restoreButton->setEnabled(picked && DataEdits::restored(m_edits, channel, start, end).size() != m_edits.size());
}

void DataPanel::removeSelected()
{
    QString channel;
    double start = 0.0;
    double end = 0.0;
    if (!selection(&channel, &start, &end))
        {
            return;
        }
    QVector<DataEdit> edits = m_edits;
    edits.append({channel, start, end});
    m_chart->clearSelection();
    m_table->clearSelection();
    emit editsChanged(edits, tr("Take out samples"));
}

void DataPanel::restoreSelected()
{
    QString channel;
    double start = 0.0;
    double end = 0.0;
    if (!selection(&channel, &start, &end))
        {
            return;
        }
    m_chart->clearSelection();
    m_table->clearSelection();
    emit editsChanged(DataEdits::restored(m_edits, channel, start, end), tr("Put back samples"));
}
