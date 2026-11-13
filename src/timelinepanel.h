#pragma once

#include "telemetry/telemetrysession.h"
#include "units/units.h"

#include <QWidget>

class QComboBox;
class TimelineStrip;

// The whole recording at a glance, in video time: where the video and the
// data are, a trace of one channel, the laps with their sectors and the
// pauses. Clicking seeks; Ctrl+dragging slides the data against the video.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(QWidget *parent = nullptr);

    void setSession(const TelemetrySessionPtr &session, const Units &units);
    void setVideoDuration(double seconds);
    // telemetry time = video time + offset
    void setOffset(double seconds);
    void setPosition(double videoSeconds);

signals:
    void seekRequested(double videoSeconds);
    // while the data is dragged
    void offsetDragged(double seconds);

private:
    void fillChannels();

    QComboBox *m_channelCombo{nullptr};
    TimelineStrip *m_strip{nullptr};
    TelemetrySessionPtr m_session;
    Units m_units;
};
