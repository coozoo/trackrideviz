#include "gaugetheme.h"

QJsonObject GaugeTheme::toJson() const
{
    QJsonObject object{
        {"foreground", foreground.name(QColor::HexArgb)},
        {"background", background.name(QColor::HexArgb)},
        {"accent", accent.name(QColor::HexArgb)},
        {"accent2", accent2.name(QColor::HexArgb)},
    };
    if (!font.isEmpty())
        {
            object["font"] = font;
        }
    if (fontWeight > 0)
        {
            object["fontWeight"] = fontWeight;
        }
    if (fontEverywhere)
        {
            object["fontEverywhere"] = true;
        }
    return object;
}

GaugeTheme GaugeTheme::fromJson(const QJsonObject &object)
{
    GaugeTheme theme;
    auto readColor = [&object](const char *key, QColor *color)
    {
        const QColor stored(object.value(QLatin1String(key)).toString());
        if (stored.isValid())
            {
                *color = stored;
            }
    };
    readColor("foreground", &theme.foreground);
    readColor("background", &theme.background);
    readColor("accent", &theme.accent);
    readColor("accent2", &theme.accent2);
    theme.font = object.value("font").toString();
    theme.fontWeight = qBound(0, object.value("fontWeight").toInt(), 1000);
    theme.fontEverywhere = object.value("fontEverywhere").toBool();
    return theme;
}
