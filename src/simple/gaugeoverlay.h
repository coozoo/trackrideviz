#pragma once

#include "simplecontroller.h"

#include <QPointer>
#include <QQuickItem>

// The gauges of a SimpleController drawn over the picture: placed over the video's own
// rectangle (VideoOutput's contentRect) and drawn for the player's position. Drawn by the
// processor into a picture that the graphics card shows, stretched to the item.
class GaugeOverlay : public QQuickItem
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

signals:
    void controllerChanged();
    void positionChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
    QPointer<SimpleController> m_controller;
    qint64 m_position{0};
};
