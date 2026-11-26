#include "routeview.h"

#include "maps/tilestore.h"
#include "perfmeter.h"
#include "settings/appsettings.h"

#include <QPainter>

RouteView::RouteView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(true);
    m_route.setStreets(m_streets, AppSettings::mapCredit());
    connect(this, &RouteView::colorsChanged, this, [this]() { update(); });
    // map tiles that come are drawn
    connect(TileStore::instance(), &TileStore::arrived, this, [this]()
    {
        if (m_streets)
            {
                update();
            }
    });
}

void RouteView::setController(SimpleController *controller)
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
            // other data, other laps, another line: drawn again
            connect(m_controller, &SimpleController::dataChanged, this, &RouteView::takeData);
            connect(m_controller, &SimpleController::lapsChanged, this, &RouteView::takeData);
        }
    takeData();
    emit controllerChanged();
}

void RouteView::setPosition(qint64 positionMs)
{
    if (positionMs == m_position)
        {
            return;
        }
    m_position = positionMs;
    emit positionChanged();
    if (m_controller && m_route.setTime(m_controller->telemetryTime(positionMs)))
        {
            update();
        }
}

void RouteView::setHighlightLap(int number)
{
    if (number == m_highlightLap)
        {
            return;
        }
    m_highlightLap = number;
    takeHighlight();
    emit highlightLapChanged();
    update();
}

void RouteView::setStreets(bool shown)
{
    if (shown == m_streets)
        {
            return;
        }
    m_streets = shown;
    m_route.setStreets(shown, AppSettings::mapCredit());
    emit streetsChanged();
    update();
}

void RouteView::setHint(const QString &hint)
{
    if (hint == m_hint)
        {
            return;
        }
    m_hint = hint;
    emit hintChanged();
    update();
}

void RouteView::zoomAround(double x, double y, double factor)
{
    if (factor > 0.0)
        {
            m_route.zoomAround(QPointF(x, y), factor, boundingRect());
            update();
        }
}

void RouteView::moveBy(double dx, double dy)
{
    m_route.moveBy(QPointF(dx, dy), boundingRect());
    update();
}

void RouteView::fit()
{
    m_route.fit();
    update();
}

QPointF RouteView::placeAt(double x, double y) const
{
    return m_route.placeAt(QPointF(x, y), boundingRect());
}

void RouteView::paint(QPainter *painter)
{
    static PerfMeter meter("map painted");
    const PerfMeter::Scope timed(meter);
    RoutePainter::Colors colors;
    colors.base = m_baseColor;
    colors.text = m_textColor;
    colors.placeholder = m_textColor;
    colors.highlight = m_highlightColor;
    m_route.paint(*painter, boundingRect(), colors, QFont(), m_hint);
}

void RouteView::takeData()
{
    const TelemetrySessionPtr session = m_controller ? m_controller->session() : TelemetrySessionPtr();
    m_route.setSession(session, m_controller ? m_controller->project().startFinish : StartFinishLine(),
                       m_controller ? m_controller->project().sectors : QVector<StartFinishLine>());
    m_route.setSurfaces(m_controller ? m_controller->project().surfaces : QVector<SurfaceStretch>());
    if (m_controller)
        {
            m_route.setTime(m_controller->telemetryTime(m_position));
        }
    takeHighlight();
    update();
}

void RouteView::takeHighlight()
{
    const TelemetrySessionPtr session = m_controller ? m_controller->session() : TelemetrySessionPtr();
    if (session && m_highlightLap >= 1 && m_highlightLap <= session->laps().size())
        {
            const TelemetrySession::Lap &lap = session->laps().at(m_highlightLap - 1);
            m_route.setHighlight(lap.startTime, lap.endTime);
        }
    else
        {
            m_route.setHighlight(0.0, 0.0);
        }
}
