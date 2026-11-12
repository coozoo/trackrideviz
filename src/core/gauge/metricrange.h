#pragma once

#include <QStringList>

// The scale of a dial: round numbers that cover the data.
struct MetricRange
{
    double min{0.0};
    double max{100.0};
    double step{25.0};   // between two markings

    // A scale with `markings` evenly spaced numbers (2 or more; anything less
    // is taken as 5) that holds dataMin..dataMax. Steps are 1, 2, 2.5 or 5
    // times a power of ten. Starts at zero unless the data is negative or
    // lies far from zero.
    static MetricRange covering(double dataMin, double dataMax, int markings);

    // The numbers to print along the scale.
    QStringList markingTexts(int markings) const;
};
