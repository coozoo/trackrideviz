#include "gaugeeditor.h"

#include "gauge/gaugefile.h"
#include "gauge/gaugefonts.h"
#include "gauge/gaugerenderer.h"
#include "gauge/gaugetheme.h"
#include "icons.h"
#include "laps/lapengine.h"
#include "telemetry/channelids.h"
#include "telemetry/demosession.h"
#include "telemetry/derivedchannels.h"
#include "telemetry/gears.h"
#include "units/units.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSvgRenderer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
enum PartKind
{
    ValueText, LabelText, FixedTextPart, Picture, RectanglePart, EllipsePart, ArcPart, LinePart,
    NeedlePart, BarPart, ArcFillPart, DialPart, TapePart, GraphPart, RoutePart, LapRoutePart, GearsPart, LapTablePart,
    SectorBarPart, GroupPart
};

// The parts a part holds that are shown in the list under it: a group's, or what a graph
// or a map draws at the current point.
QVector<GaugeComponent> *partsIn(GaugeComponent &part)
{
    switch (part.type)
        {
        case GaugeComponent::Group:
            return &part.children;
        case GaugeComponent::Graph:
        case GaugeComponent::TrackMap:
        case GaugeComponent::LapMap:
            return &part.knob;
        default:
            return nullptr;
        }
}

// What a part covers, in the gauge's units: for picking it and for the frame drawn around it.
QRectF boundsOf(const GaugeComponent &part)
{
    switch (part.type)
        {
        case GaugeComponent::Text:
            if (!part.boxed || part.frame.isEmpty())
                {
                    // around the anchor, about as large as the text will be
                    const int letters = part.autoText == GaugeComponent::FixedText ? int(std::max<qsizetype>(2, part.text.size())) : 4;
                    const double width = part.fontSize * 0.62 * letters;
                    const double height = part.fontSize * 1.2;
                    double left = part.frame.left();
                    if (part.alignment & Qt::AlignHCenter)
                        {
                            left -= width / 2.0;
                        }
                    else if (part.alignment & Qt::AlignRight)
                        {
                            left -= width;
                        }
                    return QRectF(left, part.frame.top() - height / 2.0, width, height);
                }
            return part.frame;
        case GaugeComponent::Shape:
            if (part.shape == GaugeComponent::Line && !part.points.isEmpty())
                {
                    QRectF bounds(part.points.first(), QSizeF(0, 0));
                    for (const QPointF &p : part.points)
                        {
                            bounds.setCoords(std::min(bounds.left(), p.x()), std::min(bounds.top(), p.y()),
                                             std::max(bounds.right(), p.x()), std::max(bounds.bottom(), p.y()));
                        }
                    const double half = std::max(part.lineWidth, 6.0) / 2.0;
                    return bounds.adjusted(-half, -half, half, half);
                }
            return part.frame;
        case GaugeComponent::Group:
        {
            QRectF bounds;
            for (const GaugeComponent &child : part.children)
                {
                    bounds = bounds.isNull() ? boundsOf(child) : bounds.united(boundsOf(child));
                }
            return bounds;
        }
        default:
            return part.frame;
        }
}

// The part somewhere else, with everything that says where it is: the point a needle turns
// around, the ends of a bar's fill, a line's points, a group's parts.
void moveBy(GaugeComponent &part, const QPointF &delta)
{
    part.frame.translate(delta);
    for (QPointF &p : part.points)
        {
            p += delta;
        }
    if (!part.gradientLine.isNull())
        {
            part.gradientLine.translate(delta);
        }
    for (GaugeManipulation &m : part.manipulations)
        {
            m.pivot += delta;
            if (m.type == GaugeManipulation::WidthClip)
                {
                    m.minX += delta.x();
                    m.maxX += delta.x();
                }
            if (m.type == GaugeManipulation::HeightClip)
                {
                    m.minY += delta.y();
                    m.maxY += delta.y();
                }
            if (!m.clipRect.isNull())
                {
                    m.clipRect.translate(delta);
                }
            if (!m.frameRect.isNull())
                {
                    m.frameRect.translate(delta);
                }
        }
    for (GaugeComponent &child : part.children)
        {
            moveBy(child, delta);
        }
}

QString describe(const GaugeComponent &part)
{
    switch (part.type)
        {
        case GaugeComponent::Text:
            switch (part.autoText)
                {
                case GaugeComponent::FixedText:
                    return GaugeEditor::tr("Text \"%1\"").arg(part.text);
                case GaugeComponent::Value:
                    return GaugeEditor::tr("Value");
                case GaugeComponent::Unit:
                    return GaugeEditor::tr("Unit");
                case GaugeComponent::ValueAndUnit:
                    return GaugeEditor::tr("Value and unit");
                case GaugeComponent::Label:
                    return GaugeEditor::tr("Name of the measurement");
                case GaugeComponent::LabelValueUnit:
                    return GaugeEditor::tr("Name, value and unit");
                case GaugeComponent::IntervalMarking:
                    return GaugeEditor::tr("Number on the scale");
                }
            return GaugeEditor::tr("Text");
        case GaugeComponent::Image:
            return part.channelIcon ? GaugeEditor::tr("Picture of the measurement")
                   : part.imagePath.isEmpty() ? GaugeEditor::tr("Picture")
                   : GaugeEditor::tr("Picture %1").arg(QFileInfo(part.imagePath).fileName());
        case GaugeComponent::Group:
            return GaugeEditor::tr("Group");
        case GaugeComponent::Graph:
            return GaugeEditor::tr("Graph");
        case GaugeComponent::TrackMap:
            return GaugeEditor::tr("Route");
        case GaugeComponent::LapMap:
            return GaugeEditor::tr("Route of the lap");
        case GaugeComponent::Gears:
            return GaugeEditor::tr("Gears");
        case GaugeComponent::LapTable:
            return GaugeEditor::tr("Lap table");
        case GaugeComponent::SectorBar:
            return GaugeEditor::tr("Sector bar");
        case GaugeComponent::Dial:
            return GaugeEditor::tr("Round scale");
        case GaugeComponent::Tape:
            return GaugeEditor::tr("Sliding scale");
        case GaugeComponent::Shape:
            switch (part.shape)
                {
                case GaugeComponent::Rectangle:
                    return GaugeEditor::tr("Rectangle");
                case GaugeComponent::Ellipse:
                    return GaugeEditor::tr("Ellipse");
                case GaugeComponent::Arc:
                    return GaugeEditor::tr("Arc");
                case GaugeComponent::Pie:
                    return GaugeEditor::tr("Pie slice");
                case GaugeComponent::Line:
                    return part.filled ? GaugeEditor::tr("Polygon") : GaugeEditor::tr("Line");
                }
            break;
        }
    return GaugeEditor::tr("Part");
}

QString movementOf(const GaugeComponent &part)
{
    QStringList words;
    for (const GaugeManipulation &m : part.manipulations)
        {
            QString word;
            switch (m.type)
                {
                case GaugeManipulation::Rotate:
                    word = GaugeEditor::tr("turns");
                    break;
                case GaugeManipulation::RadialClip:
                case GaugeManipulation::WidthClip:
                case GaugeManipulation::HeightClip:
                    word = GaugeEditor::tr("fills");
                    break;
                case GaugeManipulation::Translate:
                case GaugeManipulation::Translate2D:
                    word = GaugeEditor::tr("slides");
                    break;
                case GaugeManipulation::AnimationStrip:
                    word = GaugeEditor::tr("plays");
                    break;
                default:
                    word = GaugeEditor::tr("moves");
                    break;
                }
            if (!words.contains(word))
                {
                    words << word;
                }
        }
    return words.join(QStringLiteral(", "));
}

// Where one picture of a strip of `frames` shows: the first of them, along the longer side.
QRectF stripCell(const QRectF &frame, int frames)
{
    const int n = std::max(1, frames);
    return frame.width() >= frame.height() ? QRectF(frame.topLeft(), QSizeF(frame.width() / n, frame.height()))
           : QRectF(frame.topLeft(), QSizeF(frame.width(), frame.height() / n));
}
}

// The gauge as it will look, with the sample ride; parts are picked, moved and sized here.
class EditorCanvas : public QWidget
{
public:
    explicit EditorCanvas(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(320, 280);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    const Gauge *gauge{nullptr};
    TelemetrySessionPtr session;
    QString channel;
    double time{0.0};
    // the part picked, when the frame around it can be drawn (its place is in the gauge's units)
    std::function<GaugeComponent *()> framed;
    // picks the part under a point of the gauge; false when there is none
    std::function<bool(const QPointF &)> pick;
    std::function<void()> edited;     // while dragging
    std::function<void()> finished;   // the drag is over, or a key moved the part
    // the edges and middles a part being moved is drawn to: those of the other parts and of the gauge
    std::function<QVector<QRectF>()> others;

    void refresh()
    {
        m_bound = gauge && !gauge->isEmpty() ? BoundGauge(*gauge, channel, session, Units(), GaugeTheme()) : BoundGauge();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(60, 70, 85));
        if (!gauge)
            {
                return;
            }
        const QRectF target = gaugeRect();
        painter.setPen(QPen(QColor(255, 255, 255, 70), 1, Qt::DashLine));
        painter.drawRect(target);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!m_bound.isEmpty())
            {
                m_bound.render(painter, target, time);
            }
        if (const GaugeComponent *part = framed ? framed() : nullptr)
            {
                const QRectF bounds = toWidget(boundsOf(*part));
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(Qt::black, 3));
                painter.drawRect(bounds);
                painter.setPen(QPen(QColor(90, 200, 255), 1));
                painter.drawRect(bounds);
                if (sizable(*part))
                    {
                        painter.setBrush(QColor(90, 200, 255));
                        painter.setPen(Qt::black);
                        painter.drawRect(handleOf(bounds));
                    }
            }
        // what the part being moved is held by
        painter.setPen(QPen(QColor(255, 90, 200), 1, Qt::DashLine));
        for (const double x : std::as_const(m_guidesX))
            {
                const double at = toWidget(QRectF(x, 0, 0, 0)).left();
                painter.drawLine(QPointF(at, 0), QPointF(at, height()));
            }
        for (const double y : std::as_const(m_guidesY))
            {
                const double at = toWidget(QRectF(0, y, 0, 0)).top();
                painter.drawLine(QPointF(0, at), QPointF(width(), at));
            }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !gauge)
            {
                return;
            }
        setFocus();
        const QPointF at = toGauge(event->position());
        GaugeComponent *part = framed ? framed() : nullptr;
        if (part && sizable(*part) && handleOf(toWidget(boundsOf(*part))).adjusted(-3, -3, 3, 3).contains(event->position()))
            {
                m_drag = Size;
            }
        else if (pick && pick(at))
            {
                m_drag = Move;
            }
        else
            {
                m_drag = None;
            }
        m_last = at;
        m_pressed = at;
        if (GaugeComponent *now = framed ? framed() : nullptr)
            {
                m_origin = boundsOf(*now);
            }
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        GaugeComponent *part = framed ? framed() : nullptr;
        if (m_drag == None || !part)
            {
                return;
            }
        // whole units: gauges are drawn on a grid of them
        const QPointF at = toGauge(event->position());
        QPointF delta(std::round(at.x() - m_last.x()), std::round(at.y() - m_last.y()));
        if (m_drag == Move)
            {
                // where the mouse puts it, then drawn to an edge or a middle near by (not with Alt held)
                QRectF wanted = m_origin.translated(std::round(at.x() - m_pressed.x()), std::round(at.y() - m_pressed.y()));
                m_guidesX.clear();
                m_guidesY.clear();
                if (others && !(event->modifiers() & Qt::AltModifier))
                    {
                        const QPointF held = snap(wanted, others());
                        wanted.translate(held);
                    }
                delta = wanted.topLeft() - boundsOf(*part).topLeft();
                delta = QPointF(std::round(delta.x()), std::round(delta.y()));
                update();
            }
        if (delta.isNull())
            {
                return;
            }
        m_last += delta;
        if (m_drag == Move)
            {
                moveBy(*part, delta);
            }
        else
            {
                part->frame.setWidth(std::max(1.0, part->frame.width() + delta.x()));
                part->frame.setHeight(std::max(1.0, part->frame.height() + delta.y()));
            }
        if (edited)
            {
                edited();
            }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_guidesX.clear();
        m_guidesY.clear();
        if (m_drag != None && finished)
            {
                finished();
            }
        m_drag = None;
        update();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        GaugeComponent *part = framed ? framed() : nullptr;
        const double step = event->modifiers() & Qt::ShiftModifier ? 10.0 : 1.0;
        QPointF delta;
        switch (event->key())
            {
            case Qt::Key_Left:
                delta = QPointF(-step, 0);
                break;
            case Qt::Key_Right:
                delta = QPointF(step, 0);
                break;
            case Qt::Key_Up:
                delta = QPointF(0, -step);
                break;
            case Qt::Key_Down:
                delta = QPointF(0, step);
                break;
            default:
                QWidget::keyPressEvent(event);
                return;
            }
        if (part)
            {
                moveBy(*part, delta);
                if (finished)
                    {
                        finished();
                    }
            }
    }

private:
    enum Drag { None, Move, Size };

    static bool sizable(const GaugeComponent &part)
    {
        // a text is as large as its font, a line as its points, a group as its parts
        return !(part.type == GaugeComponent::Text && !part.boxed) && part.type != GaugeComponent::Group
               && !(part.type == GaugeComponent::Shape && part.shape == GaugeComponent::Line);
    }

    // How far `box` is to be shifted for one of its edges or its middle to meet one of
    // `targets`' within a few units, each way on its own; the lines met are kept to be drawn.
    QPointF snap(const QRectF &box, const QVector<QRectF> &targets)
    {
        const double reach = 5.0;
        double bestX = reach + 1.0;
        double bestY = reach + 1.0;
        double lineX = 0.0;
        double lineY = 0.0;
        for (const QRectF &target : targets)
            {
                for (const double to : {target.left(), target.center().x(), target.right()})
                    {
                        for (const double from : {box.left(), box.center().x(), box.right()})
                            {
                                if (std::fabs(to - from) < std::fabs(bestX))
                                    {
                                        bestX = to - from;
                                        lineX = to;
                                    }
                            }
                    }
                for (const double to : {target.top(), target.center().y(), target.bottom()})
                    {
                        for (const double from : {box.top(), box.center().y(), box.bottom()})
                            {
                                if (std::fabs(to - from) < std::fabs(bestY))
                                    {
                                        bestY = to - from;
                                        lineY = to;
                                    }
                            }
                    }
            }
        QPointF shift;
        if (std::fabs(bestX) <= reach)
            {
                shift.setX(bestX);
                m_guidesX.append(lineX);
            }
        if (std::fabs(bestY) <= reach)
            {
                shift.setY(bestY);
                m_guidesY.append(lineY);
            }
        return shift;
    }

    static QRectF handleOf(const QRectF &bounds)
    {
        return QRectF(bounds.right() - 5, bounds.bottom() - 5, 10, 10);
    }

    QRectF gaugeRect() const
    {
        const QSizeF size = gauge->size.isEmpty() ? QSizeF(400, 400) : gauge->size;
        const double scale = std::min((width() - 40.0) / size.width(), (height() - 40.0) / size.height());
        const QSizeF shown = size * std::max(scale, 0.05);
        return QRectF((width() - shown.width()) / 2.0, (height() - shown.height()) / 2.0, shown.width(), shown.height());
    }

    QPointF toGauge(const QPointF &widget) const
    {
        const QRectF target = gaugeRect();
        const double scale = target.width() / std::max(gauge->size.width(), 1.0);
        return (widget - target.topLeft()) / scale;
    }

    QRectF toWidget(const QRectF &inGauge) const
    {
        const QRectF target = gaugeRect();
        const double scale = target.width() / std::max(gauge->size.width(), 1.0);
        return QRectF(target.topLeft() + inGauge.topLeft() * scale, inGauge.size() * scale);
    }

    BoundGauge m_bound;
    Drag m_drag{None};
    QPointF m_last;
    QPointF m_pressed;           // where the drag began, in the gauge's units
    QRectF m_origin;             // where the part was then
    QVector<double> m_guidesX;
    QVector<double> m_guidesY;
};

GaugeEditor::GaugeEditor(const Gauge &gauge, const QStringList &takenNames, QWidget *parent)
    : QDialog(parent)
    , m_gauge(gauge)
    , m_cameAs(gauge.name)
    , m_takenNames(takenNames)
{
    setWindowTitle(tr("Gauge editor"));
    if (m_gauge.size.isEmpty())
        {
            m_gauge.size = QSizeF(400, 400);
        }
    // the sample ride, with laps, so that every kind of part has something to show
    const TelemetrySessionPtr ride = DerivedChannels::complete(DemoSession::create());
    Drivetrain sample;
    sample.chainrings = {34, 50};
    sample.cassette = {11, 12, 13, 14, 15, 17, 19, 21, 24, 28, 32};
    m_session = LapEngine::apply(Gears::apply(ride, sample), LapEngine::lineAt(*ride, 20.0),
                                 {LapEngine::lineAt(*ride, 110.0), LapEngine::lineAt(*ride, 210.0)});

    m_nameEdit = new QLineEdit(m_gauge.name, this);
    m_nameEdit->setPlaceholderText(tr("Name of the gauge"));
    m_widthSpin = new QDoubleSpinBox(this);
    m_heightSpin = new QDoubleSpinBox(this);
    for (QDoubleSpinBox *spin : {m_widthSpin, m_heightSpin})
        {
            spin->setRange(10, 4000);
            spin->setDecimals(0);
            spin->setToolTip(tr("The gauge's own units: parts are placed and sized in them, and the whole is scaled "
                                "to wherever the gauge is put on the video."));
        }
    m_widthSpin->setValue(m_gauge.size.width());
    m_heightSpin->setValue(m_gauge.size.height());
    auto resized = [this]()
    {
        m_gauge.size = QSizeF(m_widthSpin->value(), m_heightSpin->value());
        changed();
    };
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, resized);
    connect(m_heightSpin, &QDoubleSpinBox::valueChanged, this, resized);

    m_channelCombo = new QComboBox(this);
    m_channelCombo->setToolTip(tr("What the gauge is made for: it shows this when it is put on the video, and here. "
                                  "Any gauge can be given another measurement later."));
    for (const QString &id : m_session->channelIds())
        {
            m_channelCombo->addItem(ChannelFormat::displayFor(*m_session->channel(id), Units()).name, id);
        }
    const QString made = m_gauge.supportedTypes.isEmpty() ? QString(ChannelId::Speed) : m_gauge.supportedTypes.first();
    if (m_channelCombo->findData(made) < 0)
        {
            m_channelCombo->addItem(ChannelFormat::displayFor(made, QString(), Units()).name, made);
        }
    m_channelCombo->setCurrentIndex(m_channelCombo->findData(made));
    connect(m_channelCombo, &QComboBox::activated, this, [this]()
    {
        const QString id = m_channelCombo->currentData().toString();
        m_gauge.supportedTypes.removeAll(id);
        m_gauge.supportedTypes.prepend(id);
        changed();
    });

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Name:"), this));
    top->addWidget(m_nameEdit, 1);
    top->addWidget(new QLabel(tr("Size:"), this));
    top->addWidget(m_widthSpin);
    top->addWidget(new QLabel(QStringLiteral("×"), this));
    top->addWidget(m_heightSpin);
    top->addWidget(new QLabel(tr("Made for:"), this));
    top->addWidget(m_channelCombo);
    m_undoButton = new QPushButton(Icons::named("edit-undo"), tr("Undo"), this);
    m_undoButton->setToolTip(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText));
    connect(m_undoButton, &QPushButton::clicked, this, [this]() { stepHistory(-1); });
    m_redoButton = new QPushButton(Icons::named("edit-redo"), tr("Redo"), this);
    m_redoButton->setToolTip(QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText));
    connect(m_redoButton, &QPushButton::clicked, this, [this]() { stepHistory(1); });
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, [this]() { stepHistory(-1); });
    connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, this, [this]() { stepHistory(1); });
    top->addWidget(m_undoButton);
    top->addWidget(m_redoButton);

    // the parts
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setToolTip(tr("The gauge's parts, drawn from the first to the last: what is lower in the list lies on top"));
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this]()
    {
        if (!m_filling)
            {
                fillProperties();
                m_canvas->update();
            }
    });
    QToolButton *addButton = new QToolButton(this);
    addButton->setText(tr("Add"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    QMenu *addMenu = new QMenu(addButton);
    const QVector<QPair<QString, int>> kinds
    {
        {tr("Value"), ValueText}, {tr("Name of the measurement"), LabelText}, {tr("Text"), FixedTextPart},
        {QString(), -1},
        {tr("Needle"), NeedlePart}, {tr("Bar that fills"), BarPart}, {tr("Arc that fills"), ArcFillPart},
        {tr("Round scale"), DialPart}, {tr("Sliding scale"), TapePart},
        {QString(), -1},
        {tr("Rectangle"), RectanglePart}, {tr("Ellipse"), EllipsePart}, {tr("Arc"), ArcPart}, {tr("Line"), LinePart},
        {tr("Picture..."), Picture},
        {QString(), -1},
        {tr("Graph"), GraphPart}, {tr("Route"), RoutePart}, {tr("Route of the lap"), LapRoutePart},
        {tr("Gears (chainrings and sprockets)"), GearsPart},
        {tr("Lap table"), LapTablePart}, {tr("Sector bar"), SectorBarPart},
        {QString(), -1},
        {tr("Group"), GroupPart},
    };
    for (const auto &kind : kinds)
        {
            if (kind.second < 0)
                {
                    addMenu->addSeparator();
                    continue;
                }
            const int which = kind.second;
            addMenu->addAction(kind.first, this, [this, which]() { addPart(which); });
        }
    addButton->setMenu(addMenu);
    auto button = [this](const QString &text, const QString &tip, const std::function<void()> &pressed,
                         const QString &icon = QString())
    {
        QToolButton *made = new QToolButton(this);
        made->setText(text);
        if (!icon.isEmpty())
            {
                made->setIcon(Icons::named(icon));
                made->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            }
        made->setToolTip(tip);
        connect(made, &QToolButton::clicked, this, pressed);
        return made;
    };
    QHBoxLayout *treeButtons = new QHBoxLayout;
    treeButtons->addWidget(addButton);
    treeButtons->addWidget(button(tr("Copy"), tr("Another part like the one picked"), [this]() { duplicatePart(); }, "edit-copy"));
    treeButtons->addWidget(button(tr("Delete"), tr("Takes the picked part out of the gauge"), [this]() { removePart(); }, "edit-delete"));
    treeButtons->addWidget(button(QStringLiteral("↑"), tr("Further back: drawn earlier"), [this]() { movePart(-1); }));
    treeButtons->addWidget(button(QStringLiteral("↓"), tr("Further to the front: drawn later"), [this]() { movePart(1); }));
    treeButtons->addStretch(1);
    QWidget *left = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(m_tree, 1);
    leftLayout->addLayout(treeButtons);

    // the picture
    m_canvas = new EditorCanvas(this);
    m_canvas->gauge = &m_gauge;
    m_canvas->session = m_session;
    m_canvas->time = DemoSession::ShowcaseTime;
    m_canvas->framed = [this]() -> GaugeComponent *
    {
        // parts drawn at a graph's or a map's current point have their place counted from there
        Path path = selectedPath();
        while (path.size() > 1)
            {
                path.removeLast();
                const GaugeComponent *parent = componentAt(path);
                if (!parent || parent->type != GaugeComponent::Group)
                    {
                        return nullptr;
                    }
            }
        return selected();
    };
    m_canvas->pick = [this](const QPointF &at)
    {
        // the one in front first
        for (int i = int(m_gauge.components.size()) - 1; i >= 0; --i)
            {
                const GaugeComponent &part = m_gauge.components.at(i);
                if (boundsOf(part).adjusted(-2, -2, 2, 2).contains(at))
                    {
                        // a second click on a group picked already goes into it
                        const Path now = selectedPath();
                        if (part.type == GaugeComponent::Group && !now.isEmpty() && now.first() == i)
                            {
                                for (int c = int(part.children.size()) - 1; c >= 0; --c)
                                    {
                                        if (boundsOf(part.children.at(c)).adjusted(-2, -2, 2, 2).contains(at))
                                            {
                                                select({i, c});
                                                return true;
                                            }
                                    }
                            }
                        select({i});
                        return true;
                    }
            }
        return false;
    };
    m_canvas->others = [this]()
    {
        QVector<QRectF> targets{QRectF(QPointF(0, 0), m_gauge.size)};
        const Path now = selectedPath();
        for (int i = 0; i < m_gauge.components.size(); ++i)
            {
                if (now.isEmpty() || now.first() != i)
                    {
                        targets.append(boundsOf(m_gauge.components.at(i)));
                    }
            }
        return targets;
    };
    m_canvas->edited = [this]() { m_canvas->refresh(); };
    m_canvas->finished = [this]()
    {
        m_canvas->refresh();
        fillProperties();
        remember();
    };
    m_timeSlider = new QSlider(Qt::Horizontal, this);
    m_timeSlider->setRange(0, int(m_session->duration()));
    m_timeSlider->setValue(int(DemoSession::ShowcaseTime));
    m_timeSlider->setToolTip(tr("A moment of the sample ride: how the gauge looks with other values"));
    connect(m_timeSlider, &QSlider::valueChanged, this, [this](int seconds)
    {
        m_canvas->time = seconds;
        m_canvas->update();
    });
    QWidget *middle = new QWidget(this);
    QVBoxLayout *middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->addWidget(m_canvas, 1);
    middleLayout->addWidget(m_timeSlider);
    QLabel *hint = new QLabel(tr("Click a part to pick it (again to pick inside a group), drag it to move it, drag its "
                                 "corner to size it. A part being moved is drawn to the edges and middles of the others; "
                                 "hold Alt to move it freely. Arrow keys move it by one unit, with Shift by ten."), middle);
    hint->setWordWrap(true);
    middleLayout->addWidget(hint);

    // what the picked part is made of
    m_properties = new QWidget(this);
    m_propertiesLayout = new QVBoxLayout(m_properties);
    m_propertiesLayout->setContentsMargins(6, 0, 6, 0);
    QScrollArea *propertiesScroll = new QScrollArea(this);
    propertiesScroll->setWidgetResizable(true);
    propertiesScroll->setFrameShape(QFrame::NoFrame);
    propertiesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propertiesScroll->setMinimumWidth(370);
    propertiesScroll->setWidget(m_properties);

    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(middle);
    splitter->addWidget(propertiesScroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({230, 560, 380});

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &GaugeEditor::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &GaugeEditor::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(splitter, 1);
    layout->addWidget(buttons);
    resize(1240, 740);

    m_canvas->channel = m_channelCombo->currentData().toString();
    fillTree();
    if (!m_gauge.components.isEmpty())
        {
            select({int(m_gauge.components.size()) - 1});
        }
    fillProperties();
    m_canvas->refresh();
    remember();
}

void GaugeEditor::remember()
{
    // called while the dialog is still being put together
    if (!m_undoButton || !m_canvas)
        {
            return;
        }
    const QByteArray json = GaugeFile::toJson(m_gauge);
    if (m_historyAt < 0 || json != m_historyJson.at(m_historyAt))
        {
            // what was undone is gone once something new is done
            m_history.resize(m_historyAt + 1);
            m_historyJson.resize(m_historyAt + 1);
            m_history.append(m_gauge);
            m_historyJson.append(json);
            if (m_history.size() > 200)
                {
                    m_history.removeFirst();
                    m_historyJson.removeFirst();
                }
            m_historyAt = int(m_history.size()) - 1;
        }
    m_undoButton->setEnabled(m_historyAt > 0);
    m_redoButton->setEnabled(m_historyAt + 1 < m_history.size());
}

void GaugeEditor::stepHistory(int by)
{
    const int to = m_historyAt + by;
    if (to < 0 || to >= m_history.size())
        {
            return;
        }
    m_historyAt = to;
    const Path picked = selectedPath();
    // the name is not part of it: it is typed, and typing has an undo of its own
    const QString name = m_gauge.name;
    m_gauge = m_history.at(to);
    m_gauge.name = name;
    {
        const QSignalBlocker width(m_widthSpin);
        const QSignalBlocker height(m_heightSpin);
        m_widthSpin->setValue(m_gauge.size.width());
        m_heightSpin->setValue(m_gauge.size.height());
    }
    fillTree();
    select(picked);
    fillProperties();
    m_canvas->refresh();
    m_undoButton->setEnabled(m_historyAt > 0);
    m_redoButton->setEnabled(m_historyAt + 1 < m_history.size());
}

void GaugeEditor::accept()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        {
            QMessageBox::information(this, windowTitle(), tr("Give the gauge a name."));
            m_nameEdit->setFocus();
            return;
        }
    if (m_gauge.components.isEmpty())
        {
            QMessageBox::information(this, windowTitle(), tr("The gauge has no parts yet: add one with \"Add\"."));
            return;
        }
    if (m_reservedNames.contains(name, Qt::CaseInsensitive))
        {
            QMessageBox::information(this, windowTitle(),
                                     tr("A gauge that comes with the program is called \"%1\". Give yours another name.").arg(name));
            m_nameEdit->setFocus();
            m_nameEdit->selectAll();
            return;
        }
    if (name.compare(m_cameAs, Qt::CaseInsensitive) != 0 && m_takenNames.contains(name, Qt::CaseInsensitive)
            && QMessageBox::question(this, windowTitle(), tr("There is a gauge \"%1\" already. Replace it?").arg(name))
            != QMessageBox::Yes)
        {
            return;
        }
    m_gauge.name = name;
    const QString made = m_channelCombo->currentData().toString();
    m_gauge.supportedTypes.removeAll(made);
    m_gauge.supportedTypes.prepend(made);
    QDialog::accept();
}

QVector<GaugeComponent> *GaugeEditor::listAt(const Path &parent)
{
    QVector<GaugeComponent> *list = &m_gauge.components;
    for (const int index : parent)
        {
            if (!list || index < 0 || index >= list->size())
                {
                    return nullptr;
                }
            list = partsIn((*list)[index]);
        }
    return list;
}

GaugeComponent *GaugeEditor::componentAt(const Path &path)
{
    if (path.isEmpty())
        {
            return nullptr;
        }
    QVector<GaugeComponent> *list = listAt(path.mid(0, path.size() - 1));
    return list && path.last() >= 0 && path.last() < list->size() ? &(*list)[path.last()] : nullptr;
}

GaugeEditor::Path GaugeEditor::selectedPath() const
{
    Path path;
    for (const QTreeWidgetItem *item = m_tree->currentItem(); item; item = item->parent())
        {
            path.prepend(item->parent() ? item->parent()->indexOfChild(const_cast<QTreeWidgetItem *>(item))
                         : m_tree->indexOfTopLevelItem(const_cast<QTreeWidgetItem *>(item)));
        }
    return path;
}

GaugeComponent *GaugeEditor::selected()
{
    return componentAt(selectedPath());
}

void GaugeEditor::select(const Path &path)
{
    QTreeWidgetItem *item = nullptr;
    for (const int index : path)
        {
            item = item ? item->child(index) : m_tree->topLevelItem(index);
            if (!item)
                {
                    return;
                }
        }
    m_tree->setCurrentItem(item);
}

void GaugeEditor::fillTree()
{
    const Path keep = selectedPath();
    m_filling = true;
    m_tree->clear();
    std::function<void(QTreeWidgetItem *, QVector<GaugeComponent> &)> fill = [&](QTreeWidgetItem *parent, QVector<GaugeComponent> &parts)
    {
        for (GaugeComponent &part : parts)
            {
                QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
                const QString movement = movementOf(part);
                item->setText(0, movement.isEmpty() ? describe(part) : tr("%1 (%2)").arg(describe(part), movement));
                if (QVector<GaugeComponent> *inside = partsIn(part))
                    {
                        fill(item, *inside);
                    }
            }
    };
    fill(nullptr, m_gauge.components);
    m_tree->expandAll();
    m_filling = false;
    select(keep);
}

void GaugeEditor::changed(bool listChanged)
{
    if (listChanged)
        {
            fillTree();
            fillProperties();
        }
    else if (QTreeWidgetItem *item = m_tree->currentItem())
        {
            if (const GaugeComponent *part = selected())
                {
                    const QString movement = movementOf(*part);
                    item->setText(0, movement.isEmpty() ? describe(*part) : tr("%1 (%2)").arg(describe(*part), movement));
                }
        }
    m_canvas->channel = m_channelCombo->currentData().toString();
    m_canvas->refresh();
    remember();
}

void GaugeEditor::addPart(int kind)
{
    const double w = m_gauge.size.width();
    const double h = m_gauge.size.height();
    const double unit = std::min(w, h);
    const QPointF middle(w / 2.0, h / 2.0);
    GaugeComponent part;
    auto movement = [](GaugeManipulation::Type type)
    {
        GaugeManipulation m;
        m.type = type;
        return m;
    };
    switch (kind)
        {
        case ValueText:
        case LabelText:
        case FixedTextPart:
            part.type = GaugeComponent::Text;
            part.autoText = kind == ValueText ? GaugeComponent::Value : kind == LabelText ? GaugeComponent::Label
                            : GaugeComponent::FixedText;
            part.text = tr("Text");
            part.fontSize = std::round(kind == ValueText ? unit * 0.2 : unit * 0.08);
            part.bold = kind == ValueText;
            part.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
            part.color.role = GaugeColor::Foreground;
            // the value in the middle, words under it
            part.frame = QRectF(QPointF(middle.x(), kind == ValueText ? middle.y() : std::round(h * 0.68)), QSizeF(0, 0));
            break;
        case Picture:
        {
            QSettings settings;
            const QString file = QFileDialog::getOpenFileName(this, tr("Picture"), settings.value("paths/gaugePicture").toString(),
                                 tr("Pictures (*.svg *.png *.jpg *.jpeg *.webp);;All files (*)"));
            if (file.isEmpty())
                {
                    return;
                }
            settings.setValue("paths/gaugePicture", QFileInfo(file).absolutePath());
            QSizeF size;
            if (file.endsWith(".svg", Qt::CaseInsensitive))
                {
                    QFile svg(file);
                    if (svg.open(QIODevice::ReadOnly))
                        {
                            part.svg = svg.readAll();
                        }
                    size = QSvgRenderer(part.svg).defaultSize();
                }
            else
                {
                    part.image = GaugeFile::readImage(file);
                    size = part.image.size();
                }
            if (size.isEmpty())
                {
                    QMessageBox::warning(this, windowTitle(), tr("%1 is not a picture this program can read.").arg(QFileInfo(file).fileName()));
                    return;
                }
            part.type = GaugeComponent::Image;
            // numbered: two pictures of one name from different folders must not share a file
            part.imagePath = QString("images/%1-%2").arg(m_gauge.components.size() + 1).arg(QFileInfo(file).fileName());
            // whole if it fits, else as large as the gauge
            size = size.width() > w || size.height() > h ? size.scaled(m_gauge.size, Qt::KeepAspectRatio) : size;
            part.frame = QRectF(middle.x() - size.width() / 2.0, middle.y() - size.height() / 2.0, size.width(), size.height());
            break;
        }
        case RectanglePart:
        case EllipsePart:
            part.type = GaugeComponent::Shape;
            part.shape = kind == RectanglePart ? GaugeComponent::Rectangle : GaugeComponent::Ellipse;
            part.color.role = GaugeColor::Background;
            part.cornerRadius = kind == RectanglePart ? std::round(unit * 0.04) : 0.0;
            part.frame = QRectF(w * 0.1, h * 0.1, w * 0.8, h * 0.8);
            break;
        case ArcPart:
        case ArcFillPart:
            part.type = GaugeComponent::Shape;
            part.shape = GaugeComponent::Arc;
            part.filled = false;
            part.lineWidth = std::round(unit * 0.05);
            part.startAngle = -135.0;
            part.sweepAngle = 270.0;
            part.color.role = kind == ArcFillPart ? GaugeColor::Accent : GaugeColor::Foreground;
            part.frame = QRectF(middle.x() - unit * 0.4, middle.y() - unit * 0.4, unit * 0.8, unit * 0.8);
            if (kind == ArcFillPart)
                {
                    GaugeManipulation m = movement(GaugeManipulation::RadialClip);
                    m.pivot = middle;
                    m.minAngle = -135.0;
                    m.maxAngle = 135.0;
                    part.manipulations.append(m);
                }
            break;
        case LinePart:
            part.type = GaugeComponent::Shape;
            part.shape = GaugeComponent::Line;
            part.filled = false;
            part.lineWidth = 4.0;
            part.color.role = GaugeColor::Foreground;
            part.points = {QPointF(w * 0.2, middle.y()), QPointF(w * 0.8, middle.y())};
            break;
        case NeedlePart:
        {
            // drawn pointing up, from a little below the middle
            part.type = GaugeComponent::Shape;
            part.shape = GaugeComponent::Rectangle;
            part.color.role = GaugeColor::Accent;
            const double width = std::max(4.0, std::round(unit * 0.02));
            part.cornerRadius = width / 2.0;
            part.frame = QRectF(middle.x() - width / 2.0, middle.y() - unit * 0.38, width, unit * 0.44);
            GaugeManipulation m = movement(GaugeManipulation::Rotate);
            m.pivot = middle;
            m.minAngle = -135.0;
            m.maxAngle = 135.0;
            part.manipulations.append(m);
            break;
        }
        case BarPart:
        {
            part.type = GaugeComponent::Shape;
            part.shape = GaugeComponent::Rectangle;
            part.color.role = GaugeColor::Accent;
            part.frame = QRectF(w * 0.1, middle.y() - unit * 0.05, w * 0.8, unit * 0.1);
            GaugeManipulation m = movement(GaugeManipulation::WidthClip);
            m.minX = part.frame.left();
            m.maxX = part.frame.right();
            part.manipulations.append(m);
            break;
        }
        case DialPart:
            part.type = GaugeComponent::Dial;
            part.frame = QRectF(middle.x() - unit / 2.0, middle.y() - unit / 2.0, unit, unit);
            part.color.role = GaugeColor::Foreground;
            part.fontSize = std::round(unit * 0.06);
            part.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        {
            // the ends a needle goes by when it has no scale of its own: what the measurement usually spans
            // (asked of a gauge that surely shows the measurement: one text with its value)
            Gauge probe;
            GaugeComponent value;
            value.type = GaugeComponent::Text;
            value.autoText = GaugeComponent::Value;
            probe.components.append(value);
            const MetricRange usual = BoundGauge(probe, m_channelCombo->currentData().toString(), m_session, Units(), GaugeTheme())
                                      .rangeOf();
            part.scale.minValue = usual.min;
            part.scale.maxValue = usual.max;
            part.scale.major.unit = usual.step;
            part.scale.minor.unit = usual.step / 5.0;
        }
            part.scale.startAngle = -135.0;
            part.scale.endAngle = 135.0;
            part.scale.radius = std::round(unit * 0.46);
            part.scale.arcWidth = 3.0;
            part.scale.arcColor.role = GaugeColor::Foreground;
            part.scale.major = {part.scale.major.unit, std::round(unit * 0.05), 3.0, {GaugeColor::Foreground, QColor()}};
            part.scale.minor = {part.scale.minor.unit, std::round(unit * 0.025), 1.5, {GaugeColor::Foreground, QColor()}};
            part.scale.labelOffset = std::round(unit * 0.11);
            break;
        case TapePart:
            part.type = GaugeComponent::Tape;
            part.frame = QRectF(w * 0.1, middle.y() - unit * 0.1, w * 0.8, unit * 0.2);
            part.color.role = GaugeColor::Foreground;
            part.fontSize = std::round(unit * 0.06);
            part.alignment = Qt::AlignHCenter | Qt::AlignVCenter;
            part.scale.minValue = 0.0;
            part.scale.maxValue = 100.0;
            part.scale.length = w * 0.8;
            part.scale.spacing = std::round(w * 0.16);
            part.scale.major = {10.0, std::round(unit * 0.06), 3.0, {GaugeColor::Foreground, QColor()}};
            part.scale.minor = {2.0, std::round(unit * 0.03), 1.5, {GaugeColor::Foreground, QColor()}};
            part.scale.labelOffset = std::round(unit * 0.04);
            part.scale.markerLength = std::round(unit * 0.1);
            part.scale.markerWidth = 3.0;
            part.scale.markerColor.role = GaugeColor::Accent;
            break;
        case GraphPart:
        case RoutePart:
        case LapRoutePart:
        {
            part.type = kind == GraphPart ? GaugeComponent::Graph : kind == RoutePart ? GaugeComponent::TrackMap
                        : GaugeComponent::LapMap;
            part.frame = QRectF(w * 0.05, h * 0.05, w * 0.9, h * 0.9);
            part.color.role = GaugeColor::Foreground;
            part.lineWidth = std::max(2.0, std::round(unit * 0.012));
            part.padding = kind == GraphPart ? 0.0 : std::round(unit * 0.05);
            // a dot at the current point
            GaugeComponent dot;
            dot.type = GaugeComponent::Shape;
            dot.shape = GaugeComponent::Ellipse;
            dot.color.role = GaugeColor::Accent;
            const double size = std::max(8.0, std::round(unit * 0.04));
            dot.frame = QRectF(-size / 2.0, -size / 2.0, size, size);
            part.knob.append(dot);
            break;
        }
        case GearsPart:
            part.type = GaugeComponent::Gears;
            part.frame = QRectF(w * 0.05, h * 0.2, w * 0.9, h * 0.6);
            part.color.role = GaugeColor::Foreground;
            part.activeColor.role = GaugeColor::Accent;
            part.lineWidth = std::max(2.0, std::round(unit * 0.01));
            break;
        case LapTablePart:
            part.type = GaugeComponent::LapTable;
            part.frame = QRectF(w * 0.08, h * 0.1, w * 0.84, h * 0.8);
            part.color.role = GaugeColor::Foreground;
            part.activeColor.role = GaugeColor::Accent;
            part.fontSize = std::round(part.frame.height() / part.rows * 0.6);
            break;
        case SectorBarPart:
            part.type = GaugeComponent::SectorBar;
            part.frame = QRectF(w * 0.05, h * 0.4, w * 0.9, h * 0.2);
            part.color.role = GaugeColor::Foreground;
            part.activeColor.role = GaugeColor::Accent;
            part.padding = std::round(unit * 0.02);
            part.cornerRadius = std::round(unit * 0.02);
            break;
        case GroupPart:
            part.type = GaugeComponent::Group;
            break;
        default:
            return;
        }
    // after the picked part, in the list it is in; into a picked group
    Path path = selectedPath();
    QVector<GaugeComponent> *list = &m_gauge.components;
    int at = int(list->size());
    if (GaugeComponent *picked = selected())
        {
            if (picked->type == GaugeComponent::Group)
                {
                    list = &picked->children;
                    at = int(list->size());
                    path.append(at);
                }
            else if (QVector<GaugeComponent> *own = listAt(path.mid(0, path.size() - 1)))
                {
                    list = own;
                    at = path.last() + 1;
                    path.last() = at;
                }
        }
    else
        {
            path = {at};
        }
    list->insert(at, part);
    fillTree();
    select(path);
    fillProperties();
    changed();
}

void GaugeEditor::removePart()
{
    Path path = selectedPath();
    QVector<GaugeComponent> *list = path.isEmpty() ? nullptr : listAt(path.mid(0, path.size() - 1));
    if (!list || path.last() >= list->size())
        {
            return;
        }
    list->removeAt(path.last());
    fillTree();
    if (list->isEmpty())
        {
            path.removeLast();
        }
    else
        {
            path.last() = std::min(path.last(), int(list->size()) - 1);
        }
    select(path);
    fillProperties();
    changed();
}

void GaugeEditor::duplicatePart()
{
    Path path = selectedPath();
    QVector<GaugeComponent> *list = path.isEmpty() ? nullptr : listAt(path.mid(0, path.size() - 1));
    if (!list || path.last() >= list->size())
        {
            return;
        }
    GaugeComponent copy = list->at(path.last());
    // a little aside, or it would hide the one it was made from
    moveBy(copy, QPointF(10, 10));
    list->insert(path.last() + 1, copy);
    path.last() += 1;
    fillTree();
    select(path);
    fillProperties();
    changed();
}

void GaugeEditor::movePart(int by)
{
    Path path = selectedPath();
    QVector<GaugeComponent> *list = path.isEmpty() ? nullptr : listAt(path.mid(0, path.size() - 1));
    if (!list)
        {
            return;
        }
    const int to = path.last() + by;
    if (to < 0 || to >= list->size())
        {
            return;
        }
    list->swapItemsAt(path.last(), to);
    path.last() = to;
    fillTree();
    select(path);
    changed();
}

// --- the properties ---------------------------------------------------------

void GaugeEditor::addNumber(QFormLayout *form, const QString &label, double value, double low, double high, int decimals,
                            const std::function<void(Part &, double)> &set, const QString &tip)
{
    QDoubleSpinBox *spin = new QDoubleSpinBox(m_properties);
    spin->setRange(low, high);
    spin->setDecimals(decimals);
    spin->setValue(value);
    spin->setToolTip(tip);
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, set](double now)
    {
        if (Part *part = selected())
            {
                set(*part, now);
                changed();
            }
    });
    form->addRow(label, spin);
}

void GaugeEditor::addCheck(QFormLayout *form, const QString &label, bool value, const std::function<void(Part &, bool)> &set,
                           const QString &tip, bool refill)
{
    QCheckBox *check = new QCheckBox(label, m_properties);
    check->setChecked(value);
    check->setToolTip(tip);
    connect(check, &QCheckBox::toggled, this, [this, set, refill](bool now)
    {
        if (Part *part = selected())
            {
                set(*part, now);
                changed();
                if (refill)
                    {
                        // other rows depend on this one; not from inside its own signal
                        QMetaObject::invokeMethod(this, [this]() { fillProperties(); }, Qt::QueuedConnection);
                    }
            }
    });
    form->addRow(check);
}

void GaugeEditor::addChoice(QFormLayout *form, const QString &label, const QVector<QPair<QString, int>> &choices, int value,
                            const std::function<void(Part &, int)> &set, bool refill)
{
    QComboBox *combo = new QComboBox(m_properties);
    for (const auto &choice : choices)
        {
            combo->addItem(choice.first, choice.second);
        }
    combo->setCurrentIndex(std::max(0, combo->findData(value)));
    connect(combo, &QComboBox::activated, this, [this, combo, set, refill]()
    {
        if (Part *part = selected())
            {
                set(*part, combo->currentData().toInt());
                changed();
                if (refill)
                    {
                        QMetaObject::invokeMethod(this, [this]() { fillProperties(); }, Qt::QueuedConnection);
                    }
            }
    });
    form->addRow(label, combo);
}

void GaugeEditor::addText(QFormLayout *form, const QString &label, const QString &value,
                          const std::function<void(Part &, const QString &)> &set)
{
    QLineEdit *edit = new QLineEdit(value, m_properties);
    connect(edit, &QLineEdit::textEdited, this, [this, set](const QString &now)
    {
        if (Part *part = selected())
            {
                set(*part, now);
                changed();
            }
    });
    form->addRow(label, edit);
}

void GaugeEditor::addColor(QFormLayout *form, const QString &label, const GaugeColor &value,
                           const std::function<void(Part &, const GaugeColor &)> &set)
{
    QComboBox *combo = new QComboBox(m_properties);
    combo->setToolTip(tr("Colors of the theme change with the theme chosen for a video; a color of its own stays."));
    combo->addItem(tr("As it is"), int(GaugeColor::Original));
    combo->addItem(tr("Theme: text"), int(GaugeColor::Foreground));
    combo->addItem(tr("Theme: background"), int(GaugeColor::Background));
    combo->addItem(tr("Theme: accent"), int(GaugeColor::Accent));
    combo->addItem(tr("Theme: 2nd accent"), int(GaugeColor::Accent2));
    combo->addItem(tr("Its own..."), int(GaugeColor::Custom));
    combo->setCurrentIndex(std::max(0, combo->findData(int(value.role))));
    QToolButton *swatch = new QToolButton(m_properties);
    auto show = [swatch](const GaugeColor &color)
    {
        QPixmap picture(16, 16);
        picture.fill(color.role == GaugeColor::Custom && color.custom.isValid() ? color.custom : Qt::transparent);
        swatch->setIcon(QIcon(picture));
        swatch->setEnabled(color.role == GaugeColor::Custom);
    };
    show(value);
    auto current = std::make_shared<GaugeColor>(value);
    auto apply = [this, set, show, current]()
    {
        show(*current);
        if (Part *part = selected())
            {
                set(*part, *current);
                changed();
            }
    };
    auto choose = [this, current, apply]()
    {
        const QColor chosen = QColorDialog::getColor(current->custom.isValid() ? current->custom : QColor(Qt::white), this,
                              tr("Color"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            {
                current->custom = chosen;
                apply();
            }
    };
    connect(combo, &QComboBox::activated, this, [combo, current, apply, choose]()
    {
        current->role = GaugeColor::Role(combo->currentData().toInt());
        if (current->role == GaugeColor::Custom && !current->custom.isValid())
            {
                current->custom = Qt::white;
                apply();
                choose();
                return;
            }
        apply();
    });
    connect(swatch, &QToolButton::clicked, this, choose);
    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(combo, 1);
    row->addWidget(swatch);
    form->addRow(label, row);
}

void GaugeEditor::addFont(QFormLayout *form, const Part &part)
{
    QComboBox *combo = new QComboBox(m_properties);
    combo->addItem(tr("The gauges' own (%1)").arg(QLatin1String(GaugeFonts::Family)), QString());
    for (const QString &family : QFontDatabase::families())
        {
            if (family != QLatin1String(GaugeFonts::Family))
                {
                    combo->addItem(family, family);
                }
        }
    // one this computer does not have stays what the gauge asks for
    if (!part.fontFamily.isEmpty() && combo->findData(part.fontFamily) < 0)
        {
            combo->addItem(tr("%1 (not on this computer)").arg(part.fontFamily), part.fontFamily);
        }
    combo->setCurrentIndex(std::max(0, combo->findData(part.fontFamily)));
    combo->setToolTip(tr("The gauges' own font comes with the program and looks the same everywhere. Another one is "
                         "only named in the gauge: a computer without it draws the text in a font of its own."));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(14);
    connect(combo, &QComboBox::activated, this, [this, combo]()
    {
        if (Part *picked = selected())
            {
                picked->fontFamily = combo->currentData().toString();
                changed();
            }
    });
    form->addRow(tr("Font:"), combo);
}

void GaugeEditor::addChannel(QFormLayout *form, const QString &label, const QString &value,
                             const std::function<void(Part &, const QString &)> &set)
{
    QComboBox *combo = new QComboBox(m_properties);
    combo->setToolTip(tr("What this part shows. \"The gauge's\" follows whatever the gauge is given on the video; "
                         "a measurement of its own stays (a compass inside a speedometer)."));
    combo->addItem(tr("The gauge's"), QString());
    for (const QString &id : m_session->channelIds())
        {
            combo->addItem(ChannelFormat::displayFor(*m_session->channel(id), Units()).name, id);
        }
    if (combo->findData(value) < 0)
        {
            combo->addItem(ChannelFormat::displayFor(value, QString(), Units()).name, value);
        }
    combo->setCurrentIndex(combo->findData(value));
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(14);
    connect(combo, &QComboBox::activated, this, [this, combo, set]()
    {
        if (Part *part = selected())
            {
                set(*part, combo->currentData().toString());
                changed();
            }
    });
    form->addRow(label, combo);
}

void GaugeEditor::addMovement(QFormLayout *form, const Part &part, int index)
{
    // movement `index` of the part's, applied in their order; the first one is where a part gets one
    const bool has = index < part.manipulations.size();
    const GaugeManipulation m = has ? part.manipulations.at(index) : GaugeManipulation();
    const double far = 10000.0;
    addChoice(form, tr("With the value it:"),
    {
        {index == 0 ? tr("stays as it is") : tr("(no more: remove this movement)"), -1},
        {tr("turns (a needle)"), int(GaugeManipulation::Rotate)},
        {tr("fills round (an arc)"), int(GaugeManipulation::RadialClip)},
        {tr("fills sideways (a bar)"), int(GaugeManipulation::WidthClip)},
        {tr("fills upwards (a column)"), int(GaugeManipulation::HeightClip)},
        {tr("slides"), int(GaugeManipulation::Translate)},
        {tr("moves by two values (a g-force ball)"), int(GaugeManipulation::Translate2D)},
        {tr("plays a picture strip (a beating heart)"), int(GaugeManipulation::AnimationStrip)},
    }, has ? int(m.type) : -1, [this, index](Part &p, int type)
    {
        const bool fresh = index >= p.manipulations.size();
        if (type < 0)
            {
                if (!fresh)
                    {
                        p.manipulations.removeAt(index);
                    }
                return;
            }
        GaugeManipulation made = fresh ? GaugeManipulation() : p.manipulations.at(index);
        const bool changedType = fresh || made.type != GaugeManipulation::Type(type);
        made.type = GaugeManipulation::Type(type);
        const QRectF bounds = boundsOf(p);
        const double unit = std::min(m_gauge.size.width(), m_gauge.size.height());
        // sensible ends to start from
        if (fresh || (made.minAngle == 0.0 && made.maxAngle == 0.0))
            {
                made.pivot = QPointF(m_gauge.size.width() / 2.0, m_gauge.size.height() / 2.0);
                made.minAngle = -135.0;
                made.maxAngle = 135.0;
            }
        if (type == int(GaugeManipulation::WidthClip))
            {
                made.minX = bounds.left();
                made.maxX = bounds.right();
            }
        else if (type == int(GaugeManipulation::HeightClip))
            {
                made.minY = bounds.bottom();
                made.maxY = bounds.top();
            }
        else if (type == int(GaugeManipulation::Translate) && fresh)
            {
                made.minX = 0.0;
                made.maxX = bounds.width();
                made.minY = 0.0;
                made.maxY = 0.0;
            }
        else if (type == int(GaugeManipulation::Translate2D) && changedType)
            {
                // as the G-plot has it: sideways to the right, braking upwards, one g at the edge
                made.maxRadius = std::round(unit * 0.4);
                made.xChannel = ChannelId::GForceLateral;
                made.yChannel = ChannelId::GForceLongitudinal;
                made.invertY = true;
                made.hasValueRange = true;
                made.minValue = -1.0;
                made.maxValue = 1.0;
            }
        else if (type == int(GaugeManipulation::AnimationStrip) && changedType)
            {
                made.frames = std::max(made.frames, 4);
                made.minFps = 1.0;
                made.maxFps = 4.0;
                made.frameRect = stripCell(p.frame, made.frames);
            }
        if (fresh)
            {
                p.manipulations.append(made);
            }
        else
            {
                p.manipulations[index] = made;
            }
    }, true);
    if (!has)
        {
            return;
        }
    auto at = [index](Part &p) -> GaugeManipulation & { return p.manipulations[index]; };
    switch (m.type)
        {
        case GaugeManipulation::Rotate:
        case GaugeManipulation::RadialClip:
            addNumber(form, tr("Around X:"), m.pivot.x(), -far, far, 1, [at](Part &p, double v) { at(p).pivot.setX(v); },
                      tr("The point it turns around, in the gauge's units"));
            addNumber(form, tr("Around Y:"), m.pivot.y(), -far, far, 1, [at](Part &p, double v) { at(p).pivot.setY(v); });
            addNumber(form, tr("Angle at the lowest:"), m.minAngle, -720, 720, 1, [at](Part &p, double v) { at(p).minAngle = v; },
                      tr("Degrees clockwise, 0 = straight up. A needle is drawn pointing up and turned by this."));
            addNumber(form, tr("Angle at the highest:"), m.maxAngle, -720, 720, 1, [at](Part &p, double v) { at(p).maxAngle = v; });
            break;
        case GaugeManipulation::WidthClip:
            addNumber(form, tr("Empty at X:"), m.minX, -far, far, 1, [at](Part &p, double v) { at(p).minX = v; },
                      tr("Where the fill starts, and how far it has come at the highest value"));
            addNumber(form, tr("Full at X:"), m.maxX, -far, far, 1, [at](Part &p, double v) { at(p).maxX = v; });
            addNumber(form, tr("Slant of the edge:"), m.clipAngle, -80, 80, 1, [at](Part &p, double v) { at(p).clipAngle = v; });
            break;
        case GaugeManipulation::HeightClip:
            addNumber(form, tr("Empty at Y:"), m.minY, -far, far, 1, [at](Part &p, double v) { at(p).minY = v; },
                      tr("Where the fill starts (the bottom, for one that rises), and how far it has come at the highest value"));
            addNumber(form, tr("Full at Y:"), m.maxY, -far, far, 1, [at](Part &p, double v) { at(p).maxY = v; });
            break;
        case GaugeManipulation::Translate:
            addNumber(form, tr("Moved in X, lowest:"), m.minX, -far, far, 1, [at](Part &p, double v) { at(p).minX = v; });
            addNumber(form, tr("Moved in X, highest:"), m.maxX, -far, far, 1, [at](Part &p, double v) { at(p).maxX = v; });
            addNumber(form, tr("Moved in Y, lowest:"), m.minY, -far, far, 1, [at](Part &p, double v) { at(p).minY = v; });
            addNumber(form, tr("Moved in Y, highest:"), m.maxY, -far, far, 1, [at](Part &p, double v) { at(p).maxY = v; });
            break;
        case GaugeManipulation::Translate2D:
            addChannel(form, tr("Sideways it follows:"), m.xChannel, [at](Part &p, const QString &id) { at(p).xChannel = id; });
            addChannel(form, tr("Up and down it follows:"), m.yChannel, [at](Part &p, const QString &id) { at(p).yChannel = id; });
            addNumber(form, tr("Reaches as far as:"), m.maxRadius, 0, far, 1, [at](Part &p, double v) { at(p).maxRadius = v; },
                      tr("From where it is drawn, in the gauge's units, at the largest value"));
            addCheck(form, tr("Sideways the other way"), m.invertX, [at](Part &p, bool on) { at(p).invertX = on; });
            addCheck(form, tr("Up and down the other way"), m.invertY, [at](Part &p, bool on) { at(p).invertY = on; });
            addNumber(form, tr("Trail, seconds:"), m.trailSeconds, 0, 60, 1, [at](Part &p, double v) { at(p).trailSeconds = v; },
                      tr("The way it went over the last seconds, drawn behind it, fading; 0: none"));
            break;
        case GaugeManipulation::AnimationStrip:
        {
            QLabel *note = new QLabel(tr("The picture holds the strip's pictures side by side (or one above the other); "
                                         "one shows at a time, played faster the higher the value."), m_properties);
            note->setWordWrap(true);
            form->addRow(note);
            addNumber(form, tr("Pictures in the strip:"), m.frames, 1, 500, 0, [at](Part &p, double v)
            {
                at(p).frames = int(v);
                at(p).frameRect = stripCell(p.frame, int(v));
            });
            addNumber(form, tr("Pictures a second, lowest:"), m.minFps, 0, 240, 1, [at](Part &p, double v) { at(p).minFps = v; });
            addNumber(form, tr("Pictures a second, highest:"), m.maxFps, 0, 240, 1, [at](Part &p, double v) { at(p).maxFps = v; });
            break;
        }
        default:
            form->addRow(new QLabel(tr("A movement this editor does not change; it is kept as it is."), m_properties));
            return;
        }
    addCheck(form, tr("A scale of its own"), m.hasValueRange, [at](Part &p, bool on) { at(p).hasValueRange = on; },
             tr("Off: from the lowest to the highest the measurement usually has, whatever it is. On: between the two "
                "values below - give a needle the same ends as the round scale it points at."), true);
    if (m.hasValueRange)
        {
            addNumber(form, tr("Lowest value:"), m.minValue, -1e6, 1e6, 2, [at](Part &p, double v) { at(p).minValue = v; });
            addNumber(form, tr("Highest value:"), m.maxValue, -1e6, 1e6, 2, [at](Part &p, double v) { at(p).maxValue = v; });
        }
    if (m.type != GaugeManipulation::Translate2D && m.type != GaugeManipulation::AnimationStrip)
        {
            addCheck(form, tr("Past the end it starts over (a compass)"), m.wrapValue, [at](Part &p, bool on) { at(p).wrapValue = on; });
        }
}

void GaugeEditor::addScale(QFormLayout *form, const Part &part)
{
    const GaugeScale &s = part.scale;
    const double far = 10000.0;
    addNumber(form, tr("From:"), s.minValue, -1e6, 1e6, 2, [](Part &p, double v) { p.scale.minValue = v; },
              tr("The values at the ends of the scale, in the units shown"));
    addNumber(form, tr("To:"), s.maxValue, -1e6, 1e6, 2, [](Part &p, double v) { p.scale.maxValue = v; });
    if (part.type == GaugeComponent::Dial)
        {
            addNumber(form, tr("Starts at angle:"), s.startAngle, -720, 720, 1, [](Part &p, double v) { p.scale.startAngle = v; },
                      tr("Degrees clockwise, 0 = straight up"));
            addNumber(form, tr("Ends at angle:"), s.endAngle, -720, 720, 1, [](Part &p, double v) { p.scale.endAngle = v; });
            addNumber(form, tr("Radius:"), s.radius, 0, far, 1, [](Part &p, double v) { p.scale.radius = v; });
            addNumber(form, tr("Width of the arc:"), s.arcWidth, 0, 500, 1, [](Part &p, double v) { p.scale.arcWidth = v; },
                      tr("0: no arc, only ticks and numbers"));
            addColor(form, tr("Color of the arc:"), s.arcColor, [](Part &p, const GaugeColor &c) { p.scale.arcColor = c; });
        }
    else
        {
            addChoice(form, tr("Numbers:"), {{tr("above"), int(GaugeScale::NumbersAbove)}, {tr("below"), int(GaugeScale::NumbersBelow)},
                {tr("on the left"), int(GaugeScale::NumbersLeft)}, {tr("on the right"), int(GaugeScale::NumbersRight)}
            }, int(s.orientation), [](Part &p, int v) { p.scale.orientation = GaugeScale::Orientation(v); });
            addNumber(form, tr("Length shown:"), s.length, 1, far, 1, [](Part &p, double v) { p.scale.length = v; });
            addNumber(form, tr("Between two numbers:"), s.spacing, 1, far, 1, [](Part &p, double v) { p.scale.spacing = v; },
                      tr("Distance between two large ticks, in the gauge's units"));
            addCheck(form, tr("After the end it starts over (a compass)"), s.loop, [](Part &p, bool on) { p.scale.loop = on; });
            addCheck(form, tr("Values grow the other way"), s.reverse, [](Part &p, bool on) { p.scale.reverse = on; });
            addNumber(form, tr("Mark at the value, length:"), s.markerLength, 0, far, 1, [](Part &p, double v) { p.scale.markerLength = v; },
                      tr("0: none"));
            addNumber(form, tr("Mark, width:"), s.markerWidth, 0, 100, 1, [](Part &p, double v) { p.scale.markerWidth = v; });
            addColor(form, tr("Color of the mark:"), s.markerColor, [](Part &p, const GaugeColor &c) { p.scale.markerColor = c; });
        }
    addNumber(form, tr("A number every:"), s.major.unit, 0, 1e6, 2, [](Part &p, double v) { p.scale.major.unit = v; },
              tr("Large ticks, with a number each; 0: none"));
    addNumber(form, tr("Large tick, length:"), s.major.length, 0, far, 1, [](Part &p, double v) { p.scale.major.length = v; });
    addNumber(form, tr("Large tick, width:"), s.major.width, 0, 100, 1, [](Part &p, double v) { p.scale.major.width = v; });
    addColor(form, tr("Large ticks:"), s.major.color, [](Part &p, const GaugeColor &c) { p.scale.major.color = c; });
    addNumber(form, tr("A small tick every:"), s.minor.unit, 0, 1e6, 2, [](Part &p, double v) { p.scale.minor.unit = v; },
              tr("0: none"));
    addNumber(form, tr("Small tick, length:"), s.minor.length, 0, far, 1, [](Part &p, double v) { p.scale.minor.length = v; });
    addNumber(form, tr("Small tick, width:"), s.minor.width, 0, 100, 1, [](Part &p, double v) { p.scale.minor.width = v; });
    addColor(form, tr("Small ticks:"), s.minor.color, [](Part &p, const GaugeColor &c) { p.scale.minor.color = c; });
    addNumber(form, tr("Numbers set in by:"), s.labelOffset, -far, far, 1, [](Part &p, double v) { p.scale.labelOffset = v; });
    addNumber(form, tr("Size of the numbers:"), part.fontSize, 1, 2000, 0, [](Part &p, double v) { p.fontSize = v; });
}

void GaugeEditor::fillProperties()
{
    // the old rows go, with their layouts
    while (QLayoutItem *item = m_propertiesLayout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
                {
                    widget->hide();
                    widget->deleteLater();
                }
            if (QLayout *layout = item->layout())
                {
                    while (QLayoutItem *inner = layout->takeAt(0))
                        {
                            if (inner->widget())
                                {
                                    inner->widget()->hide();
                                    inner->widget()->deleteLater();
                                }
                            if (QLayout *row = inner->layout())
                                {
                                    while (QLayoutItem *last = row->takeAt(0))
                                        {
                                            if (last->widget())
                                                {
                                                    last->widget()->hide();
                                                    last->widget()->deleteLater();
                                                }
                                            delete last;
                                        }
                                }
                            delete inner;
                        }
                }
            delete item;
        }
    const Part *picked = selected();
    if (!picked)
        {
            QLabel *hint = new QLabel(tr("Pick a part in the list or in the picture, or add one with \"Add\"."), m_properties);
            hint->setWordWrap(true);
            m_propertiesLayout->addWidget(hint);
            m_propertiesLayout->addStretch(1);
            return;
        }
    const Part part = *picked;   // rows below read from a copy: the list may change under them
    auto section = [this](const QString &title)
    {
        QLabel *heading = new QLabel(QString("<b>%1</b>").arg(title.toHtmlEscaped()), m_properties);
        m_propertiesLayout->addWidget(heading);
        QFormLayout *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        m_propertiesLayout->addLayout(form);
        return form;
    };
    const double far = 10000.0;
    const bool text = part.type == GaugeComponent::Text;
    const bool shape = part.type == GaugeComponent::Shape;
    const bool line = shape && part.shape == GaugeComponent::Line;
    const bool route = part.type == GaugeComponent::TrackMap || part.type == GaugeComponent::LapMap;

    QFormLayout *place = section(describe(part));
    if (line && part.points.size() >= 2)
        {
            for (int i = 0; i < part.points.size(); ++i)
                {
                    addNumber(place, tr("Point %1, X:").arg(i + 1), part.points.at(i).x(), -far, far, 1,
                              [i](Part &p, double v) { if (i < p.points.size()) p.points[i].setX(v); });
                    addNumber(place, tr("Point %1, Y:").arg(i + 1), part.points.at(i).y(), -far, far, 1,
                              [i](Part &p, double v) { if (i < p.points.size()) p.points[i].setY(v); });
                }
            QPushButton *more = new QPushButton(tr("Add a point"), m_properties);
            more->setToolTip(tr("After the last one, as far on again as the last step"));
            QPushButton *fewer = new QPushButton(tr("Remove the last point"), m_properties);
            fewer->setEnabled(part.points.size() > 2);
            const auto points = [this](bool add)
            {
                if (Part *p = selected())
                    {
                        if (add)
                            {
                                const QPointF last = p->points.last();
                                const QPointF step = last - p->points.at(p->points.size() - 2);
                                p->points.append(last + (step.isNull() ? QPointF(10, 0) : step));
                            }
                        else if (p->points.size() > 2)
                            {
                                p->points.removeLast();
                            }
                        changed();
                        QMetaObject::invokeMethod(this, [this]() { fillProperties(); }, Qt::QueuedConnection);
                    }
            };
            connect(more, &QPushButton::clicked, this, [points]() { points(true); });
            connect(fewer, &QPushButton::clicked, this, [points]() { points(false); });
            QHBoxLayout *buttons = new QHBoxLayout;
            buttons->addWidget(more);
            buttons->addWidget(fewer);
            place->addRow(buttons);
        }
    else if (part.type != GaugeComponent::Group)
        {
            const QString tip = text ? tr("The point the text is set against, in the gauge's units") : tr("In the gauge's units, from its top left corner");
            addNumber(place, tr("X:"), part.frame.x(), -far, far, 1, [](Part &p, double v) { p.frame.moveLeft(v); }, tip);
            addNumber(place, tr("Y:"), part.frame.y(), -far, far, 1, [](Part &p, double v) { p.frame.moveTop(v); }, tip);
            if (!text || part.boxed)
                {
                    addNumber(place, tr("Width:"), part.frame.width(), 0, far, 1, [](Part &p, double v) { p.frame.setWidth(v); });
                    addNumber(place, tr("Height:"), part.frame.height(), 0, far, 1, [](Part &p, double v) { p.frame.setHeight(v); });
                }
        }
    if (part.type != GaugeComponent::Group)
        {
            addColor(place, tr("Color:"), part.color, [](Part &p, const GaugeColor &c) { p.color = c; });
        }
    addChannel(place, tr("Shows:"), part.channel, [](Part &p, const QString &id) { p.channel = id; });
    addCheck(place, tr("Not drawn where there is no data"), part.hideWhenUnknown, [](Part &p, bool on) { p.hideWhenUnknown = on; });

    if (text)
        {
            QFormLayout *form = section(tr("Text"));
            addChoice(form, tr("Says:"),
            {
                {tr("the value"), int(GaugeComponent::Value)}, {tr("the unit"), int(GaugeComponent::Unit)},
                {tr("the value and the unit"), int(GaugeComponent::ValueAndUnit)},
                {tr("the measurement's name"), int(GaugeComponent::Label)},
                {tr("name, value and unit"), int(GaugeComponent::LabelValueUnit)},
                {tr("a number of the scale"), int(GaugeComponent::IntervalMarking)},
                {tr("these words:"), int(GaugeComponent::FixedText)},
            }, int(part.autoText), [](Part &p, int v) { p.autoText = GaugeComponent::AutoText(v); }, true);
            if (part.autoText == GaugeComponent::FixedText)
                {
                    addText(form, tr("Words:"), part.text, [](Part &p, const QString &words) { p.text = words; });
                }
            addFont(form, part);
            addNumber(form, tr("Size:"), part.fontSize, 1, 2000, 0, [](Part &p, double v) { p.fontSize = v; },
                      tr("Height of the letters, in the gauge's units"));
            addChoice(form, tr("Set:"), {{tr("to the right of the point"), int(Qt::AlignLeft)}, {tr("around the point"), int(Qt::AlignHCenter)},
                {tr("to the left of the point"), int(Qt::AlignRight)}
            }, int(part.alignment & Qt::AlignHorizontal_Mask), [](Part &p, int v)
            {
                p.alignment = Qt::Alignment(v) | Qt::AlignVCenter;
            });
            addCheck(form, tr("Bold"), part.bold, [](Part &p, bool on) { p.bold = on; });
            addCheck(form, tr("Italic"), part.italic, [](Part &p, bool on) { p.italic = on; });
            addCheck(form, tr("CAPITALS"), part.upperCase, [](Part &p, bool on) { p.upperCase = on; });
            addCheck(form, tr("In a box, wrapped at its width"), part.boxed, [this](Part &p, bool on)
            {
                p.boxed = on;
                // a box to start from, a few lines high, from the point the text was set against
                if (on && (p.frame.width() <= 0.0 || p.frame.height() <= 0.0))
                    {
                        p.frame.setSize(QSizeF(std::round(m_gauge.size.width() * 0.6), std::round(p.fontSize * 2.5)));
                    }
            }, tr("The text inside the frame below, aligned in it and broken into lines where it is too long"), true);
            if (part.autoText != GaugeComponent::FixedText && part.autoText != GaugeComponent::Unit
                    && part.autoText != GaugeComponent::Label)
                {
                    addChoice(form, tr("Written as:"),
                    {
                        {tr("a number"), int(GaugeComponent::Number)}, {tr("a whole number, cut off"), int(GaugeComponent::WholeNumber)},
                        {tr("minutes:seconds"), int(GaugeComponent::Duration)},
                        {tr("degrees, minutes, seconds"), int(GaugeComponent::DegreesMinutesSeconds)},
                        {tr("the time of day"), int(GaugeComponent::Clock)},
                    }, int(part.valueFormat), [](Part &p, int v) { p.valueFormat = GaugeComponent::ValueFormat(v); });
                    addNumber(form, tr("Decimals:"), part.precision, -1, 6, 0, [](Part &p, double v) { p.precision = int(v); },
                              tr("-1: as many as suit the measurement"));
                    addCheck(form, tr("\"+\" before positive values"), part.showPlus, [](Part &p, bool on) { p.showPlus = on; });
                }
        }
    if (part.type == GaugeComponent::Image)
        {
            QFormLayout *form = section(tr("Picture"));
            addCheck(form, tr("The measurement's own picture"), part.channelIcon, [](Part &p, bool on) { p.channelIcon = on; },
                     tr("Instead of the picture: a heart for the heart rate, a mountain for the elevation and so on"));
            QLabel *note = new QLabel(tr("A drawing (.svg) stays sharp at any size and may use the theme's colors: "
                                         "write @foreground, @background, @accent or @accent2 where a color goes."), m_properties);
            note->setWordWrap(true);
            form->addRow(note);
        }
    if (shape)
        {
            QFormLayout *form = section(tr("Shape"));
            if (!line)
                {
                    addChoice(form, tr("Kind:"), {{tr("rectangle"), int(GaugeComponent::Rectangle)}, {tr("ellipse"), int(GaugeComponent::Ellipse)},
                        {tr("arc"), int(GaugeComponent::Arc)}, {tr("pie slice"), int(GaugeComponent::Pie)}
                    }, int(part.shape), [](Part &p, int v)
                    {
                        p.shape = GaugeComponent::ShapeKind(v);
                        if ((v == int(GaugeComponent::Arc) || v == int(GaugeComponent::Pie)) && p.sweepAngle == 0.0)
                            {
                                p.startAngle = -135.0;
                                p.sweepAngle = 270.0;
                            }
                        if (v == int(GaugeComponent::Arc))
                            {
                                p.filled = false;
                            }
                    }, true);
                    if (part.shape != GaugeComponent::Arc)
                        {
                            addCheck(form, tr("Filled"), part.filled, [](Part &p, bool on) { p.filled = on; }, QString(), true);
                        }
                }
            else
                {
                    addCheck(form, tr("Closed and filled (a polygon)"), part.filled, [](Part &p, bool on) { p.filled = on; },
                             tr("From the last point back to the first, the inside filled"), true);
                }
            if (part.filled)
                {
                    // from the color above to this one, along a line
                    const bool fades = part.gradientColor.role != GaugeColor::Original;
                    addCheck(form, tr("Fades to another color"), fades, [this](Part &p, bool on)
                    {
                        if (!on)
                            {
                                p.gradientColor = GaugeColor();
                                return;
                            }
                        // top to bottom of the shape, to the background
                        const QRectF bounds = boundsOf(p);
                        p.gradientColor.role = GaugeColor::Background;
                        p.gradientLine = QLineF(bounds.center().x(), bounds.top(), bounds.center().x(), bounds.bottom());
                    }, tr("A gradient from the color above, at the start of a line, to this one at its end"), true);
                    if (fades)
                        {
                            addColor(form, tr("Fades to:"), part.gradientColor, [](Part &p, const GaugeColor &c) { p.gradientColor = c; });
                            addNumber(form, tr("Fade from X:"), part.gradientLine.x1(), -far, far, 1, [](Part &p, double v) { p.gradientLine.setP1(QPointF(v, p.gradientLine.y1())); });
                            addNumber(form, tr("Fade from Y:"), part.gradientLine.y1(), -far, far, 1, [](Part &p, double v) { p.gradientLine.setP1(QPointF(p.gradientLine.x1(), v)); });
                            addNumber(form, tr("Fade to X:"), part.gradientLine.x2(), -far, far, 1, [](Part &p, double v) { p.gradientLine.setP2(QPointF(v, p.gradientLine.y2())); });
                            addNumber(form, tr("Fade to Y:"), part.gradientLine.y2(), -far, far, 1, [](Part &p, double v) { p.gradientLine.setP2(QPointF(p.gradientLine.x2(), v)); });
                        }
                }
            if (!part.filled || part.shape == GaugeComponent::Arc)
                {
                    addNumber(form, tr("Width of the line:"), part.lineWidth, 0.1, 1000, 1, [](Part &p, double v) { p.lineWidth = v; });
                }
            if (part.shape == GaugeComponent::Rectangle)
                {
                    addNumber(form, tr("Corners rounded by:"), part.cornerRadius, 0, far, 1, [](Part &p, double v) { p.cornerRadius = v; });
                }
            if (part.shape == GaugeComponent::Arc || part.shape == GaugeComponent::Pie)
                {
                    addNumber(form, tr("Starts at angle:"), part.startAngle, -720, 720, 1, [](Part &p, double v) { p.startAngle = v; },
                              tr("Degrees clockwise, 0 = straight up"));
                    addNumber(form, tr("Turns through:"), part.sweepAngle, -720, 720, 1, [](Part &p, double v) { p.sweepAngle = v; });
                }
        }
    if (part.type == GaugeComponent::Dial || part.type == GaugeComponent::Tape)
        {
            addScale(section(tr("Scale")), part);
        }
    if (part.type == GaugeComponent::Gears)
        {
            QFormLayout *form = section(tr("Gears"));
            addColor(form, tr("In use:"), part.activeColor, [](Part &p, const GaugeColor &c) { p.activeColor = c; });
            addNumber(form, tr("Width of the chain:"), part.lineWidth, 0.1, 1000, 1, [](Part &p, double v) { p.lineWidth = v; });
        }
    if (part.type == GaugeComponent::LapTable)
        {
            QFormLayout *form = section(tr("Lap table"));
            addNumber(form, tr("Lines:"), part.rows, 1, 50, 0, [](Part &p, double v) { p.rows = int(v); },
                      tr("The laps finished last, and the one under way at the bottom"));
            addColor(form, tr("Best lap:"), part.activeColor, [](Part &p, const GaugeColor &c) { p.activeColor = c; });
            addFont(form, part);
            addNumber(form, tr("Size:"), part.fontSize, 1, 2000, 0, [](Part &p, double v) { p.fontSize = v; },
                      tr("Height of the letters, in the gauge's units"));
            addCheck(form, tr("Bold"), part.bold, [](Part &p, bool on) { p.bold = on; });
            addCheck(form, tr("Italic"), part.italic, [](Part &p, bool on) { p.italic = on; });
            addNumber(form, tr("Decimals:"), part.precision, -1, 3, 0, [](Part &p, double v) { p.precision = int(v); },
                      tr("Of a second; -1: two"));
        }
    if (part.type == GaugeComponent::SectorBar)
        {
            QFormLayout *form = section(tr("Sector bar"));
            addColor(form, tr("Faster than the best:"), part.activeColor, [](Part &p, const GaugeColor &c) { p.activeColor = c; });
            addNumber(form, tr("Gap between sectors:"), part.padding, 0, far, 1, [](Part &p, double v) { p.padding = v; });
            addNumber(form, tr("Corners rounded by:"), part.cornerRadius, 0, far, 1, [](Part &p, double v) { p.cornerRadius = v; });
        }
    if (part.type == GaugeComponent::Graph || route)
        {
            QFormLayout *form = section(route ? tr("Route") : tr("Graph"));
            addNumber(form, tr("Width of the line:"), part.lineWidth, 0.1, 1000, 1, [](Part &p, double v) { p.lineWidth = v; });
            addNumber(form, tr("Kept free inside:"), part.padding, 0, far, 1, [](Part &p, double v) { p.padding = v; });
            if (route)
                {
                    addCheck(form, tr("On a street map"), part.tiles, [](Part &p, bool on)
                    {
                        p.tiles = on;
                        p.tileOpacity = on && p.tileOpacity >= 1.0 ? 0.75 : p.tileOpacity;
                    }, QString(), true);
                    if (part.tiles)
                        {
                            addNumber(form, tr("Strength of the map:"), part.tileOpacity, 0.05, 1, 2, [](Part &p, double v) { p.tileOpacity = v; });
                        }
                    addNumber(form, tr("Close-up, meters across:"), part.span, 0, 100000, 0, [](Part &p, double v) { p.span = v; },
                              tr("0: the whole route. Otherwise this much around the rider, the map moving under them."));
                    addCheck(form, tr("The way ahead up (close-up)"), part.headingUp, [](Part &p, bool on) { p.headingUp = on; });
                    addNumber(form, tr("Corners rounded by:"), part.cornerRadius, 0, far, 1, [](Part &p, double v) { p.cornerRadius = v; });
                }
            else
                {
                    addNumber(form, tr("Seconds before now:"), part.windowBefore, 0, 86400, 0, [](Part &p, double v) { p.windowBefore = v; },
                              tr("Both 0: the whole recording"));
                    addNumber(form, tr("Seconds after now:"), part.windowAfter, 0, 86400, 0, [](Part &p, double v) { p.windowAfter = v; });
                }
            QLabel *note = new QLabel(tr("The parts under it in the list are drawn at the current point, their place counted "
                                         "from it: X 0, Y 0 is the point itself."), m_properties);
            note->setWordWrap(true);
            form->addRow(note);
        }
    if (part.type != GaugeComponent::Dial && part.type != GaugeComponent::Tape && part.type != GaugeComponent::Graph && !route)
        {
            // in their order: a needle that turns and is also cut to an arc
            const int count = std::max(1, int(part.manipulations.size()));
            QFormLayout *last = nullptr;
            for (int i = 0; i < count; ++i)
                {
                    last = section(count > 1 ? tr("Movement %1").arg(i + 1) : tr("Movement"));
                    addMovement(last, part, i);
                }
            if (!part.manipulations.isEmpty())
                {
                    QPushButton *another = new QPushButton(tr("Add another movement"), m_properties);
                    another->setToolTip(tr("Applied after the ones above: a needle that turns and slides, an arc that fills "
                                           "and turns with the heading"));
                    connect(another, &QPushButton::clicked, this, [this]()
                    {
                        if (Part *p = selected())
                            {
                                GaugeManipulation added;
                                added.type = GaugeManipulation::Rotate;
                                added.pivot = QPointF(m_gauge.size.width() / 2.0, m_gauge.size.height() / 2.0);
                                added.minAngle = -135.0;
                                added.maxAngle = 135.0;
                                p->manipulations.append(added);
                                changed();
                                QMetaObject::invokeMethod(this, [this]() { fillProperties(); }, Qt::QueuedConnection);
                            }
                    });
                    last->addRow(another);
                }
        }
    m_propertiesLayout->addStretch(1);
}
