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

    // A scale from `min` to exactly `max` with about `intervals` steps: as
    // close to that count as gives a round step (1, 1.5, 2, 2.5, 3, 4 or 5
    // times a power of ten); `intervals` itself when none does.
    static MetricRange upTo(double min, double max, int intervals);

    // The numbers to print along the scale.
    QStringList markingTexts(int markings) const;
};
