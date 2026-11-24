#pragma once

#include "simplecontroller.h"

#include <QPointer>
#include <QQuickPaintedItem>

// The gauges of a SimpleController drawn over the picture: placed over the video's own
// rectangle (VideoOutput's contentRect) and drawn for the player's position.
class GaugeOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(SimpleController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)

public:
    explicit GaugeOverlay(QQuickItem *parent = nullptr);

    SimpleController *controller() const { return m_controller; }
    void setController(SimpleController *controller);
    qint64 position() const { return m_position; }
    void setPosition(qint64 positionMs);

    void paint(QPainter *painter) override;

signals:
    void controllerChanged();
    void positionChanged();

private:
    QPointer<SimpleController> m_controller;
    qint64 m_position{0};
};
