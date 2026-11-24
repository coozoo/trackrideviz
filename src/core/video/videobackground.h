#pragma once

#include "mediainfo.h"

#include <QColor>
#include <QJsonObject>
#include <QSize>

#include <algorithm>

// A picture in place of a camera video: the gauges over a plain color, or over nothing (see
// through), as long as the data. For a ride with no video, or a route shown on its own.
struct VideoBackground
{
    QSize size;                 // of the picture; empty = none
    int fps{30};
    QColor color{Qt::black};    // alpha 0 = see-through: only the gauges are exported
    // the largest that may be asked for
    static constexpr int kMaxSide = 8192;
    static constexpr int kMaxFps = 240;

    bool isOn() const { return !size.isEmpty() && fps > 0; }
    bool isTransparent() const { return color.alpha() == 0; }
    // What the export reads off a video, for one of this as long as `duration` (seconds).
    MediaInfo mediaInfo(double duration) const
    {
        MediaInfo info;
        info.size = size;
        info.fpsNum = fps;
        info.fpsDen = 1;
        info.duration = duration;
        info.frameCount = qint64(duration * fps);
        info.videoCodec = QStringLiteral("background");
        return info;
    }

    bool operator==(const VideoBackground &other) const
    {
        return size == other.size && fps == other.fps && color == other.color;
    }
    bool operator!=(const VideoBackground &other) const { return !(*this == other); }

    QJsonObject toJson() const
    {
        return {{"width", size.width()}, {"height", size.height()}, {"fps", fps}, {"color", color.name(QColor::HexArgb)}};
    }
    static VideoBackground fromJson(const QJsonObject &object)
    {
        VideoBackground background;
        background.size = QSize(object.value("width").toInt(), object.value("height").toInt());
        background.fps = object.value("fps").toInt(30);
        const QColor color(object.value("color").toString());
        background.color = color.isValid() ? color : QColor(Qt::black);
        // as the command line allows: a file edited by hand could ask for a picture too large to make
        background.size = background.size.boundedTo(QSize(kMaxSide, kMaxSide));
        background.fps = std::min(background.fps, kMaxFps);
        return background;
    }
};
