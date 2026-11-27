#pragma once

#include "simplecontroller.h"

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QVector>

// One channel of a SimpleController's data along the video, as the full window's timeline has
// it: its line over the whole length, the laps in bands under it, where the video is and the
// value there (valueText, for a label of its QML). Its QML seeks where it is tapped. The line is
// drawn once into a picture; the graphics card moves the cursor over it.
class ChannelGraph : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(SimpleController *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    // ms of the video: where it is, how long it is
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration WRITE setDuration NOTIFY durationChanged)
    // where the cursor is, from the left; the value there with its unit, "--" where none
    Q_PROPERTY(qreal cursorX READ cursorX NOTIFY positionChanged)
    Q_PROPERTY(QString valueText READ valueText NOTIFY valueTextChanged)
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
    qreal cursorX() const;
    QString valueText() const { return m_valueText; }

    // The laps' bands and the line at `ratio` pixels to a point: what the cursor moves over.
    QImage background(qreal ratio);

signals:
    void controllerChanged();
    void channelChanged();
    void positionChanged();
    void durationChanged();
    void colorsChanged();
    void valueTextChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
    // the line drawn again before the next picture
    void stale();
    // the value at the position, for valueText
    void updateValueText();
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
    bool m_backgroundShown{false};  // the graphics card has m_background
    QString m_valueText;
};
