#pragma once

#include <QCoreApplication>
#include <QComboBox>
#include <QSettings>

// The smoothing charts are drawn with (Data, Timeline, Compare laps): one
// setting for all of them, remembered between runs. Only for showing.
namespace ChartSmoothing
{
inline double saved()
{
    return QSettings().value("charts/smoothing", 0.0).toDouble();
}

inline void save(double seconds)
{
    QSettings().setValue("charts/smoothing", seconds);
}

// A box offering it, set to what is saved.
inline QComboBox *makeCombo(QWidget *parent)
{
    QComboBox *combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("ChartSmoothing", "Not smoothed"), 0.0);
    combo->addItem(QCoreApplication::translate("ChartSmoothing", "Smoothed a little (2 s)"), 2.0);
    combo->addItem(QCoreApplication::translate("ChartSmoothing", "Smoothed (5 s)"), 5.0);
    combo->addItem(QCoreApplication::translate("ChartSmoothing", "Smoothed a lot (15 s)"), 15.0);
    combo->setToolTip(QCoreApplication::translate("ChartSmoothing", "Evens out the lines of the charts; the data itself stays as recorded"));
    combo->setCurrentIndex(std::max(0, combo->findData(saved())));
    // narrow; the list opens as wide as it needs
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(9);
    return combo;
}

inline void select(QComboBox *combo, double seconds)
{
    const QSignalBlocker blocker(combo);
    combo->setCurrentIndex(std::max(0, combo->findData(seconds)));
}
}
