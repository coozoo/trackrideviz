#pragma once

#include "laps/lapengine.h"
#include "telemetry/surfaces.h"
#include "units/units.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class TrackView;

// The route seen from above with the start/finish and sector lines on it, and
// the laps they give. Lines are placed by clicking on the route.
class LapsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LapsPanel(QWidget *parent = nullptr);

    void setSession(const TelemetrySessionPtr &session, const StartFinishLine &line,
                    const QVector<StartFinishLine> &sectors);
    // Telemetry time on screen, for the position on the map and the lap under way.
    void setTime(double time);

    // The map chosen in the settings may have changed.
    void refreshMapChoice();
    // Stretches of the ride marked as a surface, drawn along the route.
    void setSurfaces(const QVector<SurfaceStretch> &surfaces);
    // A stretch of time drawn red on the map: what is picked in the Data viewer.
    void setHighlight(double start, double end);
    // Kilometers or miles in the table of surfaces.
    void setUnits(const Units &units);
    // The laps selected in the table, by number from 0.
    QVector<int> selectedLaps() const;

signals:
    // Another map was chosen here: the settings hold it already.
    void mapChoiceChanged();
    // The user clicked here on the map to place the line.
    void lineRequested(double lat, double lon);
    void removeLineRequested();
    // a line where the route gives the most laps
    void findLineRequested();
    void sectorRequested(double lat, double lon);
    void removeSectorsRequested();
    // The user clicked where a stretch begins and where it ends, and chose what it is ridden on.
    void surfaceRequested(double fromLat, double fromLon, double toLat, double toLon, const QString &surface,
                          double factor);
    void removeSurfacesRequested();
    // The list of surfaces was changed in its table: a kind, where one begins or ends, one removed.
    void surfacesEdited(const QVector<SurfaceStretch> &surfaces);
    // Telemetry time to show: a lap was double-clicked.
    void seekRequested(double time);
    // the laps selected in the table, as indexes into the session's laps
    void lapsSelected(const QVector<int> &laps);
    // the lap others are measured against: its number from 1; 0 = the fastest
    void referenceChosen(int lap);

private:
    void placeToggled(QPushButton *button, bool checked);
    void surfaceClicked(double lat, double lon);
    void lapActivated(QTreeWidgetItem *item);
    void updateInfo();

    TrackView *m_track{nullptr};
    QCheckBox *m_streetsCheck{nullptr};
    QComboBox *m_mapCombo{nullptr};
    QPushButton *m_placeButton{nullptr};
    QPushButton *m_removeButton{nullptr};
    QPushButton *m_findButton{nullptr};
    QPushButton *m_sectorButton{nullptr};
    QPushButton *m_clearSectorsButton{nullptr};
    QPushButton *m_surfaceButton{nullptr};
    QPushButton *m_clearSurfacesButton{nullptr};
    QPushButton *m_removeSurfaceButton{nullptr};
    QTableWidget *m_surfaceTable{nullptr};
    QVector<SurfaceStretch> m_surfaces;
    bool m_surfacesShown{false};   // the table shows m_surfaces in the units of now
    Units m_units;
    // where the stretch being marked begins, once clicked
    bool m_surfaceBegun{false};
    double m_surfaceLat{0.0};
    double m_surfaceLon{0.0};
    QLabel *m_info{nullptr};
    QTreeWidget *m_lapTable{nullptr};
    TelemetrySessionPtr m_session;
    StartFinishLine m_line;
    QVector<StartFinishLine> m_sectors;
};
