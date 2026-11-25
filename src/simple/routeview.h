#pragma once

#include "simplecontroller.h"

#include "maps/routepainter.h"

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>

// The route of a SimpleController's data from above, as the laps map of the full window draws it
// (RoutePainter): its start/finish line, a lap broad and red, where the rider is at `position`,
// streets under it. Zoomed and moved by its QML, which asks where a tap is.
class RouteView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(SimpleController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    // the lap drawn broad, by its number; 0: none
    Q_PROPERTY(int highlightLap READ highlightLap WRITE setHighlightLap NOTIFY highlightLapChanged)
    Q_PROPERTY(bool streets READ streets WRITE setStreets NOTIFY streetsChanged)
    // what a tap does, written at the top; empty: nothing
    Q_PROPERTY(QString hint READ hint WRITE setHint NOTIFY hintChanged)
    Q_PROPERTY(QColor baseColor MEMBER m_baseColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor highlightColor MEMBER m_highlightColor NOTIFY colorsChanged)

public:
    explicit RouteView(QQuickItem *parent = nullptr);

    SimpleController *controller() const { return m_controller; }
    void setController(SimpleController *controller);
    qint64 position() const { return m_position; }
    void setPosition(qint64 positionMs);
    int highlightLap() const { return m_highlightLap; }
    void setHighlightLap(int number);
    bool streets() const { return m_streets; }
    void setStreets(bool shown);
    QString hint() const { return m_hint; }
    void setHint(const QString &hint);

    // Closer by `factor` about (x, y); moved by (dx, dy) pixels; the whole route again.
    Q_INVOKABLE void zoomAround(double x, double y, double factor);
    Q_INVOKABLE void moveBy(double dx, double dy);
    Q_INVOKABLE void fit();
    // The place under (x, y): x longitude, y latitude.
    Q_INVOKABLE QPointF placeAt(double x, double y) const;

    void paint(QPainter *painter) override;

signals:
    void controllerChanged();
    void positionChanged();
    void highlightLapChanged();
    void streetsChanged();
    void hintChanged();
    void colorsChanged();

private:
    void takeData();
    void takeHighlight();

    QPointer<SimpleController> m_controller;
    RoutePainter m_route;
    qint64 m_position{0};
    int m_highlightLap{0};
    bool m_streets{true};
    QString m_hint;
    QColor m_baseColor{Qt::white};
    QColor m_textColor{Qt::black};
    QColor m_highlightColor{40, 120, 220};
};
