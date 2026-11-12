#pragma once

#include <QColor>

// The colours gauges are painted in. One theme for the whole project, so
// gauges from different authors look like one set.
struct GaugeTheme
{
    QColor foreground{255, 255, 255};
    QColor background{0, 0, 0, 150};
    QColor accent{255, 80, 40};
    QColor accent2{255, 255, 255};

    bool operator==(const GaugeTheme &other) const
    {
        return foreground == other.foreground && background == other.background
               && accent == other.accent && accent2 == other.accent2;
    }
    bool operator!=(const GaugeTheme &other) const { return !(*this == other); }
};
