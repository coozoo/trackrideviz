#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

// The colours and the font gauges are painted in. One theme for the whole
// project, so gauges from different authors look like one set.
struct GaugeTheme
{
    QColor foreground{255, 255, 255};
    QColor background{0, 0, 0, 150};
    QColor accent{255, 80, 40};
    QColor accent2{255, 255, 255};

    // Texts: empty / 0 = as each gauge is designed. The font replaces ours
    // (GaugeFonts) and no font at all; with fontEverywhere also fonts that
    // gauges name themselves.
    QString font;
    int fontWeight{0};           // 100 thin .. 900 black, as in CSS
    bool fontEverywhere{false};

    bool operator==(const GaugeTheme &other) const
    {
        return foreground == other.foreground && background == other.background
               && accent == other.accent && accent2 == other.accent2 && font == other.font
               && fontWeight == other.fontWeight && fontEverywhere == other.fontEverywhere;
    }
    bool operator!=(const GaugeTheme &other) const { return !(*this == other); }

    // As projects and templates store it.
    QJsonObject toJson() const;
    // Anything missing keeps its default.
    static GaugeTheme fromJson(const QJsonObject &object);
};
