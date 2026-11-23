#pragma once

#include "telemetry/gpsproblems.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

// The GPS problems of the data (GpsProblems), to go to and fix one by one or all at once.
// Stays open beside the window; show() it again with the data as corrected after each fix.
class GpsProblemsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GpsProblemsDialog(QWidget *parent = nullptr);

    // The data as recorded and its corrections so far.
    void show(const TelemetrySessionPtr &recorded, const QVector<DataEdit> &edits);

signals:
    // a problem chosen: from..to of the data
    void chosen(double start, double end);
    // the corrections with the fixes added
    void fixed(const QVector<DataEdit> &edits, const QString &description);

private:
    void fix(bool all);
    void updateButtons();

    QVector<GpsProblems::Problem> m_problems;
    QVector<DataEdit> m_edits;
    QTreeWidget *m_list{nullptr};
    QLabel *m_summary{nullptr};
    QPushButton *m_fixButton{nullptr};
    QPushButton *m_fixAllButton{nullptr};
};
