#pragma once

#include "gauge/gauge.h"
#include "telemetry/telemetrysession.h"

#include <QDialog>

#include <functional>

class EditorCanvas;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QPushButton;
class QSlider;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// Makes a gauge from nothing or changes one: its parts as a list (drawn from
// the first to the last), a picture of it with the sample ride where parts
// are picked, moved and sized with the mouse, and what the picked part is
// made of. Nothing is stored here: the caller takes gauge() when the dialog
// was accepted.
class GaugeEditor : public QDialog
{
    Q_OBJECT

public:
    // `takenNames`: gauges of the user's there are already, to ask before one is replaced;
    // the name the gauge came with is not asked about.
    GaugeEditor(const Gauge &gauge, const QStringList &takenNames, QWidget *parent = nullptr);

    Gauge gauge() const { return m_gauge; }
    // Names the gauge cannot be saved under: those of the gauges that come with the program.
    void setReservedNames(const QStringList &names) { m_reservedNames = names; }

    void accept() override;

private:
    // Where a part is: its number among the gauge's parts, then among the parts inside it
    // (a group's, or what a graph or a map draws at the current point), and so on.
    using Path = QVector<int>;

    QVector<GaugeComponent> *listAt(const Path &parent);
    GaugeComponent *componentAt(const Path &path);
    GaugeComponent *selected();
    Path selectedPath() const;
    void select(const Path &path);

    void fillTree();
    void fillProperties();
    // The gauge changed: the picture is drawn again; with `listChanged` the list of parts too.
    void changed(bool listChanged = false);

    void addPart(int kind);
    void removePart();
    void duplicatePart();
    void movePart(int by);

    // Every state the gauge went through here, to go back and forth between.
    void remember();
    void stepHistory(int by);

    // rows of the properties, each writing into the part picked when it is used
    using Part = GaugeComponent;
    void addNumber(QFormLayout *form, const QString &label, double value, double low, double high, int decimals,
                   const std::function<void(Part &, double)> &set, const QString &tip = QString());
    void addCheck(QFormLayout *form, const QString &label, bool value, const std::function<void(Part &, bool)> &set,
                  const QString &tip = QString(), bool refill = false);
    void addChoice(QFormLayout *form, const QString &label, const QVector<QPair<QString, int>> &choices, int value,
                   const std::function<void(Part &, int)> &set, bool refill = false);
    void addText(QFormLayout *form, const QString &label, const QString &value,
                 const std::function<void(Part &, const QString &)> &set);
    void addColor(QFormLayout *form, const QString &label, const GaugeColor &value,
                  const std::function<void(Part &, const GaugeColor &)> &set);
    void addFont(QFormLayout *form, const Part &part);
    void addChannel(QFormLayout *form, const QString &label, const QString &value,
                    const std::function<void(Part &, const QString &)> &set);
    void addMovement(QFormLayout *form, const Part &part);
    void addScale(QFormLayout *form, const Part &part);

    Gauge m_gauge;
    QString m_cameAs;
    QStringList m_takenNames;
    QStringList m_reservedNames;
    TelemetrySessionPtr m_session;

    QLineEdit *m_nameEdit{nullptr};
    QDoubleSpinBox *m_widthSpin{nullptr};
    QDoubleSpinBox *m_heightSpin{nullptr};
    QComboBox *m_channelCombo{nullptr};
    QTreeWidget *m_tree{nullptr};
    EditorCanvas *m_canvas{nullptr};
    QSlider *m_timeSlider{nullptr};
    QWidget *m_properties{nullptr};
    QVBoxLayout *m_propertiesLayout{nullptr};
    bool m_filling{false};
    QVector<Gauge> m_history;
    QVector<QByteArray> m_historyJson;   // the same as text, to tell a change from none
    int m_historyAt{-1};
    QPushButton *m_undoButton{nullptr};
    QPushButton *m_redoButton{nullptr};
};
