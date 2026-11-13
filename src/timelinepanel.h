#pragma once

#include "telemetry/telemetrysession.h"
#include "sync/syncmap.h"
#include "units/units.h"
#include "video/cutlist.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QToolButton;
class TimelineStrip;

// The whole recording at a glance, in video time: where the video and the
// data are, a trace of one channel, the laps with their sectors, the pauses
// and the parts cut out. Clicking seeks; Ctrl+dragging slides the data
// against the video. Parts are cut between marks A and B (set from the
// playhead, or by Shift+dragging), or where the data stood still.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(QWidget *parent = nullptr);

    void setSession(const TelemetrySessionPtr &session, const Units &units);
    void setVideoDuration(double seconds);
    void setSync(const SyncMap &sync);
    void setPosition(double videoSeconds);
    void setCuts(const QVector<TimeRange> &cuts);

signals:
    void seekRequested(double videoSeconds);
    // while the data is dragged: the new offset of the sync map
    void offsetDragged(double seconds);
    // the user changed what is cut; `description` names it for undo
    void cutsChanged(const QVector<TimeRange> &cuts, const QString &description);

private:
    void fillChannels();
    void updateCutButtons();
    void findStops();

    QComboBox *m_channelCombo{nullptr};
    QToolButton *m_cutButton{nullptr};
    QToolButton *m_keepButton{nullptr};
    QToolButton *m_clearButton{nullptr};
    QToolButton *m_stopsButton{nullptr};
    QLabel *m_keptLabel{nullptr};
    double m_videoDuration{0.0};
    SyncMap m_sync;
    QVector<TimeRange> m_cuts;
    TimelineStrip *m_strip{nullptr};
    TelemetrySessionPtr m_session;
    Units m_units;
};
