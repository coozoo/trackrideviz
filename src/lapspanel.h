#pragma once

#include "laps/lapengine.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class TrackView;

// The route seen from above with the start/finish line on it, and the laps
// it gives. The line is placed by clicking on the route.
class LapsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LapsPanel(QWidget *parent = nullptr);

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line);
    // Telemetry time on screen, for the position on the map and the lap under way.
    void setTime(double time);

signals:
    // The user clicked here on the map to place the line.
    void lineRequested(double lat, double lon);
    void removeLineRequested();
    // Telemetry time to show: a lap was double-clicked.
    void seekRequested(double time);

private:
    void placeToggled(bool checked);
    void lapActivated(QTreeWidgetItem *item);
    void updateInfo();

    TrackView *m_track{nullptr};
    QPushButton *m_placeButton{nullptr};
    QPushButton *m_removeButton{nullptr};
    QLabel *m_info{nullptr};
    QTreeWidget *m_lapTable{nullptr};
    TelemetrySessionPtr m_session;
    StartFinishLine m_line;
};
