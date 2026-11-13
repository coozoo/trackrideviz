#pragma once

#include "telemetry/dataedits.h"
#include "units/units.h"

#include <QWidget>

#include <memory>
#include <vector>

class DataChart;
class DataTableModel;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QTableView;
class QToolButton;

// The data itself: chosen channels as charts on one time axis (the data's
// own), and the samples of one as a table. Samples of a stretch can be taken
// out - a GPS jump, a dropout - and put back; everything worked out from the
// data follows.
class DataPanel : public QWidget
{
    Q_OBJECT

public:
    explicit DataPanel(QWidget *parent = nullptr);

    // raw: as recorded; session: corrected and completed (what is shown)
    void setSessions(const TelemetrySessionPtr &raw, const TelemetrySessionPtr &session, const Units &units);
    void setEdits(const QVector<DataEdit> &edits);
    // telemetry time on screen
    void setTime(double time);
    // seconds charts are smoothed over; 0 = not
    void setSmoothing(double seconds);

signals:
    void seekRequested(double time);
    void editsChanged(const QVector<DataEdit> &edits, const QString &description);
    void smoothingChanged(double seconds);

private:
    void fillChannels();
    void updateCharts();
    void showTableOf(const QString &channelId);
    void removeSelected();
    void restoreSelected();
    void updateButtons();
    // channel and time range picked in the chart or the table; false when none
    bool selection(QString *channel, double *start, double *end) const;

    QListWidget *m_channelList{nullptr};
    DataChart *m_chart{nullptr};
    QTableView *m_table{nullptr};
    DataTableModel *m_model{nullptr};
    QToolButton *m_removeButton{nullptr};
    QToolButton *m_restoreButton{nullptr};
    QToolButton *m_tableButton{nullptr};
    QComboBox *m_smoothingCombo{nullptr};
    double m_smoothing{0.0};
    std::vector<std::unique_ptr<Channel>> m_smoothed;   // what the chart draws when smoothing
    TelemetrySessionPtr m_raw;
    TelemetrySessionPtr m_session;
    Units m_units;
    QVector<DataEdit> m_edits;
    QString m_tableChannel;
};
