#pragma once

#include "telemetry/telemetrysession.h"
#include "units/units.h"

#include <QWidget>

class QComboBox;
class LapCompareChart;

// Laps side by side: a channel against distance into the lap for each of
// the chosen laps, and below it the time each loses (+) or gains on the
// fastest of them at the same point.
class LapComparePanel : public QWidget
{
    Q_OBJECT

public:
    explicit LapComparePanel(QWidget *parent = nullptr);

    void setSession(const TelemetrySessionPtr &session, const Units &units);
    // indexes into the session's laps
    void setLaps(const QVector<int> &laps);

private:
    void fillChannels();
    void rebuild();

    QComboBox *m_channelCombo{nullptr};
    LapCompareChart *m_chart{nullptr};
    TelemetrySessionPtr m_session;
    Units m_units;
    QVector<int> m_laps;
};
