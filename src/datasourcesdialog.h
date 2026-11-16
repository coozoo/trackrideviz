#pragma once

#include "telemetry/datasources.h"
#include "units/units.h"

#include <QDialog>
#include <QHash>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

// The files added to the main data, their time shifts and the channels taken from each.
// Edits a copy: the project changes only when the dialog is accepted.
class DataSourcesDialog : public QDialog
{
    Q_OBJECT

public:
    // main: the main data as recorded, for its channels and clock
    DataSourcesDialog(const QString &mainPath, const TelemetrySessionPtr &main, const QVector<DataSource> &sources,
                      const Units &units, QWidget *parent = nullptr);

    QVector<DataSource> sources() const { return m_sources; }

private slots:
    void addFiles();
    void removeSelected();
    void showSelected();
    void onShiftEdited();
    void shiftFromClocks();
    void onChannelChanged();

private:
    void fillList();
    int selectedRow() const;
    // the file as read, loaded on first use; nullptr when it cannot be read
    TelemetrySessionPtr sessionOf(const QString &path);

    QString m_mainPath;
    TelemetrySessionPtr m_main;
    QVector<DataSource> m_sources;
    Units m_units;
    QHash<QString, TelemetrySessionPtr> m_loaded;
    QHash<QString, QString> m_errors;
    bool m_showing{false};

    QListWidget *m_list{nullptr};
    QPushButton *m_removeButton{nullptr};
    QWidget *m_details{nullptr};
    QDoubleSpinBox *m_shiftSpin{nullptr};
    QPushButton *m_clockButton{nullptr};
    QLabel *m_clockNote{nullptr};
    QListWidget *m_channels{nullptr};
};
