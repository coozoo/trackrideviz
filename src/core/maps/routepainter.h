#pragma once

#include "../laps/lapengine.h"
#include "../telemetry/surfaces.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

class QPainter;

// The route from above, north up, fitted into a rectangle, for a view that draws it (the laps
// map of the full window and of the simplified one): the start/finish and sector lines, a lap
// or another stretch broad and red, the surfaces marked, where the rider is, a street map under
// it. It can be looked at closer: zoomed about a point (never farther than the whole route) and
// moved (never so far that the route is out of view).
class RoutePainter
{
public:
    // The colors of the view it is drawn in.
    struct Colors
    {
        QColor base{Qt::white};
        QColor text{Qt::black};
        QColor placeholder{Qt::gray};
        QColor highlight{40, 120, 220};
    };

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors);
    void setSurfaces(const QVector<SurfaceStretch> &surfaces) { m_surfaces = surfaces; }
    // the one picked is drawn broader; -1: none
    void setPickedSurface(int index) { m_pickedSurface = index; }
    // a stretch of time drawn broad and red along the route; none when `end` is not after `start`
    void setHighlight(double start, double end)
    {
        m_highlightStart = start;
        m_highlightEnd = end;
    }
    // where a stretch being marked begins; nowhere when `shown` is false
    void setMark(bool shown, double lat = 0.0, double lon = 0.0)
    {
        m_marked = shown;
        m_mark = QPointF(lon, lat);
    }
    // a street map under the route, with this credit in its corner
    void setStreets(bool shown, const QString &credit)
    {
        m_streets = shown;
        m_credit = credit;
    }
    // where the rider is, data time; false when it was there already
    bool setTime(double time);

    bool hasRoute() const { return m_points.size() >= 2; }

    // Into `area`; `hint` written at its top (what a tap places), empty: none.
    void paint(QPainter &painter, const QRectF &area, const Colors &colors, const QFont &font,
               const QString &hint = QString()) const;

    // Closer by `factor` about `at`, the place under it staying where it is.
    void zoomAround(const QPointF &at, double factor, const QRectF &area);
    // The view moved with a finger by `pixels`.
    void moveBy(const QPointF &pixels, const QRectF &area);
    // The whole route again.
    void fit();
    // The place under `at`: x longitude, y latitude.
    QPointF placeAt(const QPointF &at, const QRectF &area) const;

private:
    void holdPan();
    void drawLine(QPainter &painter, const QTransform &map, const StartFinishLine &line, const QColor &color) const;
    QPointF projected(const QPointF &lonLat) const;
    QTransform toView(const QRectF &area) const;

    TelemetrySessionPtr m_session;
    QRectF m_bounds;             // the route, projected
    double m_zoom{1.0};          // 1: the whole route
    QPointF m_pan;               // the middle of the view from that of the route, projected
    StartFinishLine m_line;
    QVector<StartFinishLine> m_sectors;
    QVector<QPointF> m_points;   // lon, lat
    QVector<double> m_times;     // when the ride was at each of them, s
    QVector<double> m_distances; // how far into the ride each of them is, m
    double m_highlightStart{0.0};
    double m_highlightEnd{0.0};
    QVector<SurfaceStretch> m_surfaces;
    int m_pickedSurface{-1};
    bool m_marked{false};
    QPointF m_mark;              // lon, lat
    double m_time{0.0};
    bool m_streets{false};
    QString m_credit;
};
