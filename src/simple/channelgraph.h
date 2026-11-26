#pragma once

#include "simplecontroller.h"

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVector>

// One channel of a SimpleController's data along the video, as the full window's timeline has
// it: its line over the whole length, the laps in bands under it, where the video is and the
// value there. Its QML seeks where it is tapped.
class ChannelGraph : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(SimpleController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    // ms of the video: where it is, how long it is
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration WRITE setDuration NOTIFY durationChanged)
    Q_PROPERTY(QColor lineColor MEMBER m_lineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor cursorColor MEMBER m_cursorColor NOTIFY colorsChanged)

public:
    explicit ChannelGraph(QQuickItem *parent = nullptr);

    SimpleController *controller() const { return m_controller; }
    void setController(SimpleController *controller);
    QString channel() const { return m_channel; }
    void setChannel(const QString &channel);
    qint64 position() const { return m_position; }
    void setPosition(qint64 positionMs);
    qint64 duration() const { return m_duration; }
    void setDuration(qint64 durationMs);

    void paint(QPainter *painter) override;

signals:
    void controllerChanged();
    void channelChanged();
    void positionChanged();
    void durationChanged();
    void colorsChanged();

private:
    // the line drawn again before the next picture
    void stale();
    // the value of each column of pixels; NaN where the data has none
    void sample(int columns);
    // the laps' bands and the line, what does not move with the video
    void paintBackground(QPainter &painter, double msPerPixel);

    QPointer<SimpleController> m_controller;
    QString m_channel;
    qint64 m_position{0};
    qint64 m_duration{0};
    QColor m_lineColor{40, 120, 220};
    QColor m_textColor{Qt::white};
    QColor m_cursorColor{255, 179, 0};
    // what was sampled, for how many columns
    QVector<double> m_values;
    int m_sampledColumns{0};
    double m_low{0.0};
    double m_high{0.0};
    // the bands and the line, drawn once: only the cursor moves with the video
    QImage m_background;
};
