#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

// The land around the rider in 3D, seen from behind and above: heights from the terrain model
// (Terrain), a map's picture laid over them (TileStore), shaded by a low sun, fading into the sky
// far away, the route on it. Drawn in software, so the preview, an export and the command line
// draw it alike on any machine, with or without a graphics card.
//
// The camera is set as route videos usually are: high behind the rider and looking down at
// them; the land around them is worked out once and kept until they have gone a good way.
// Thread-safe: the preview and an export may draw with one at the same time.
class Flyover
{
public:
    struct Point
    {
        double latitude{0.0};
        double longitude{0.0};
        double time{0.0};       // seconds, the data's
    };

    struct View
    {
        double latitude{0.0};   // where the rider is, looked at
        double longitude{0.0};
        double heading{0.0};    // degrees from north, the way the camera looks
        double span{1000.0};    // meters across the land at the rider
        double exaggeration{1.5};   // hills this many times their height: true ones look flat
        double tilt{60.0};          // the camera: degrees from looking straight down...
        double height{100.0};       // ... this many meters above the rider
        QString mapProvider;    // the map laid on the land (TileStore::forMap)
        bool streets{true};     // false: the land in its own colors, no map
        QColor line{255, 110, 20};  // the route ridden so far
        double lineWidth{4.0};      // pixels
        double time{0.0};       // the route up to here is ridden
    };

    void setRoute(const QVector<Point> &route);

    // The picture, and where the rider is on it (pixels).
    QImage render(const QSize &size, const View &view, QPointF *rider = nullptr);
    // Everything was there for the last picture: terrain and map; otherwise they have been asked for.
    bool wasComplete() const;

    // Asks for the terrain and the map around these places now, so that an export needs no
    // connection (heights come at once where they are on disk).
    static void want(const QVector<Point> &places, double span, const QString &mapProvider, bool streets);

private:
    struct Patch
    {
        double latitude{0.0};   // the middle
        double longitude{0.0};
        double radius{0.0};     // meters to each side
        int cells{0};           // along a side
        QVector<float> heights; // (cells + 1)^2, west to east, then south to north; meters
        QVector<float> shade;   // the sun's light on each, 0..1
        QImage texture;         // the map over it (null: the land's own colors)
        QRectF textureBounds;   // projected (TileStore's): longitude, -Mercator latitude
        QString mapProvider;
        bool streets{false};
        bool complete{false};
        bool isValid() const { return cells > 0; }
    };

    bool needsPatch(const View &view) const;
    void buildPatch(const View &view);
    // Meters east and north of the patch's middle.
    QPointF local(double latitude, double longitude) const;
    double groundAt(double east, double north) const;

    mutable QMutex m_mutex;
    QVector<Point> m_route;
    Patch m_patch;
    QElapsedTimer m_built;      // since the patch was worked out: one with tiles missing waits a while
    bool m_lastComplete{true};
};
