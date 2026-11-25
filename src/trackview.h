#pragma once

#include "maps/routepainter.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

// The route from above, north up, fitted into the widget (RoutePainter). It can
// be looked at closer: the wheel or + and - zoom in and out, a drag moves the
// view, and the button between them fits the whole route again.
class TrackView : public QWidget
{
public:
    explicit TrackView(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumSize(120, 80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        const auto button = [this](const QString &text, const QString &tip, const std::function<void()> &action)
        {
            QToolButton *b = new QToolButton(this);
            b->setText(text);
            b->setToolTip(tip);
            b->setFixedSize(26, 26);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, action);
            return b;
        };
        m_zoomIn = button(QStringLiteral("+"), QCoreApplication::translate("TrackView", "Closer (also the wheel)"),
                          [this]() { zoomAround(QRectF(rect()).center(), 1.5); });
        m_fit = button(QStringLiteral("⤢"), QCoreApplication::translate("TrackView", "The whole route"), [this]()
        {
            m_route.fit();
            update();
        });
        m_zoomOut = button(QStringLiteral("−"), QCoreApplication::translate("TrackView", "Farther"),
                           [this]() { zoomAround(QRectF(rect()).center(), 1.0 / 1.5); });
    }

    // A click on the map while something is being placed (setPlacing): latitude and longitude.
    std::function<void(double, double)> clicked;

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors)
    {
        m_route.setSession(session, line, sectors);
        update();
    }

    void setSurfaces(const QVector<SurfaceStretch> &surfaces)
    {
        m_route.setSurfaces(surfaces);
        update();
    }

    // the one picked in the table is drawn broader; -1: none
    void setPickedSurface(int index)
    {
        m_route.setPickedSurface(index);
        update();
    }

    // a stretch of time drawn broad and red along the route; none when `end` is not after `start`
    void setHighlight(double start, double end)
    {
        m_route.setHighlight(start, end);
        update();
    }

    // where a stretch being marked begins; nowhere when `shown` is false
    void setMark(bool shown, double lat = 0.0, double lon = 0.0)
    {
        m_route.setMark(shown, lat, lon);
        update();
    }

    // a street map under the route, with this credit in its corner
    void setStreets(bool shown, const QString &credit)
    {
        m_route.setStreets(shown, credit);
        update();
    }

    void setTime(double time)
    {
        // asked a few times a second, also while the video stands still
        if (m_route.setTime(time))
            {
                update();
            }
    }

    // what a click places, with a hint on the map; empty: nothing
    void setPlacing(const QString &hint)
    {
        m_placing = hint;
        setCursor(hint.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        RoutePainter::Colors colors;
        colors.base = palette().color(QPalette::Base);
        colors.text = palette().color(QPalette::Text);
        colors.placeholder = palette().color(QPalette::PlaceholderText);
        colors.highlight = palette().color(QPalette::Highlight);
        m_route.paint(painter, QRectF(rect()), colors, font(), m_placing);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            {
                m_pressedAt = event->position();
                m_lastAt = event->position();
                m_dragging = false;
            }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || !m_route.hasRoute())
            {
                return;
            }
        // a few pixels are a shaky click, not a drag
        if (!m_dragging && (event->position() - m_pressedAt).manhattanLength() < 5)
            {
                return;
            }
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
        m_route.moveBy(event->position() - m_lastAt, QRectF(rect()));
        m_lastAt = event->position();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool dragged = m_dragging;
        m_dragging = false;
        setCursor(m_placing.isEmpty() ? Qt::ArrowCursor : Qt::CrossCursor);
        if (dragged || m_placing.isEmpty() || !m_route.hasRoute() || event->button() != Qt::LeftButton || !clicked)
            {
                return;
            }
        const QPointF place = m_route.placeAt(event->position(), QRectF(rect()));
        clicked(place.y(), place.x());
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double steps = event->angleDelta().y() / 120.0;
        if (steps != 0.0)
            {
                zoomAround(event->position(), std::pow(1.25, steps));
            }
        event->accept();
    }

    void resizeEvent(QResizeEvent *) override
    {
        m_zoomIn->move(6, 6);
        m_fit->move(6, 34);
        m_zoomOut->move(6, 62);
    }

private:
    // Closer by `factor`, the place under `at` staying where it is.
    void zoomAround(const QPointF &at, double factor)
    {
        m_route.zoomAround(at, factor, QRectF(rect()));
        update();
    }

    RoutePainter m_route;
    QPointF m_pressedAt;
    QPointF m_lastAt;
    bool m_dragging{false};
    QToolButton *m_zoomIn{nullptr};
    QToolButton *m_fit{nullptr};
    QToolButton *m_zoomOut{nullptr};
    QString m_placing;
};
