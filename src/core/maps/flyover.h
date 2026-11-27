#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector3D>
#include <QVector>

#include <functional>
#include <memory>

// The land around the rider in 3D, seen from behind and above: heights from the terrain model
// (Terrain), a map's picture laid over them (TileStore), shaded by a low sun, fading into the sky
// far away, the route on it. Drawn in software, so the preview, an export and the command line
// draw it alike on any machine, with or without a graphics card.
//
// The camera is set as route videos usually are: high behind the rider and looking down at
// them; the land around them is worked out once and kept until they have gone a good way.
// Thread-safe: the preview and an export may draw with one at the same time.
//
// The next land is worked out while the last is drawn where a Patience lives on the drawing
// thread (the preview, which should not stop for it); elsewhere at once (an export, whose
// pictures are then the same however fast the machine is).
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

    // While one lives, the land on this thread may be made in the background.
    class Patience
    {
    public:
        Patience();
        ~Patience();
        Q_DISABLE_COPY_MOVE(Patience)

    private:
        bool m_was;
    };

    // The land as a graphics card draws it, made once for each land.
    struct Land
    {
        int cells{0};           // along a side: (cells + 1)^2 points
        // for each point, west to east, then south to north: east, north, height (meters around
        // the middle), the map's u and v (0..1), red, green and blue (0..1, the land's own colors
        // without a map) and the sun's light on it
        QVector<float> vertices;
        QImage texture;         // the map over it; null: the points' colors
    };
    static constexpr int LandStride = 9;    // floats for each point of Land::vertices

    // What a graphics card needs for one picture of the land.
    struct Scene
    {
        std::shared_ptr<const Land> land;   // null: nothing to draw
        // the triangles, the farthest first: drawn in this order they need no depth buffer
        QVector<quint16> indices;
        QMatrix4x4 toClip;      // meters (the height times the exaggeration) to clip space
        float exaggeration{1.0f};
        float hazeFrom{0.0f};   // meters along the view where the haze begins ...
        float hazeTo{1.0f};     // ... and where it is whole
    };

    // While one lives on a thread, flyovers that gauges draw there leave the land out of their
    // picture and put it here instead, with where it goes on the painter's device (a preview
    // that draws it on the graphics card).
    class Capture
    {
    public:
        struct Item
        {
            Scene scene;
            QRectF rect;        // device pixels of the painter
            double radius{0.0}; // of its corners, the same pixels
        };

        Capture();
        ~Capture();
        Q_DISABLE_COPY_MOVE(Capture)

        static Capture *current();
        QVector<Item> items;

    private:
        Capture *m_was;
    };

    // The sky behind the land: this color at the top, the haze's from this share of the height down.
    static QColor skyColor();
    static QColor hazeColor();
    static constexpr double SkyHazeFrom = 0.55;

    void setRoute(const QVector<Point> &route);

    // The picture, and where the rider is on it (pixels).
    QImage render(const QSize &size, const View &view, QPointF *rider = nullptr);
    // As render(), the land for a graphics card instead: the route drawn with `route` (a painter
    // whose units are the pixels of `size`, clipped to it), and where the rider is.
    Scene scene(const QSize &size, const View &view, QPainter *route, QPointF *rider = nullptr);
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
        QVector<QPointF> texels;    // with a texture: each point's pixel in it
        QVector<QRgb> colors;       // without: each point's color, by its height
        QString mapProvider;
        bool streets{false};
        bool complete{false};
        std::shared_ptr<const Land> land;   // made when a graphics card first draws it
        bool isValid() const { return cells > 0; }
    };

    // A land worked out in the background, taken over when it is ready.
    struct Pending
    {
        QMutex mutex;
        bool running{false};
        bool ready{false};
        Patch patch;
    };

    // Where the camera is for a picture of `size`.
    struct Camera
    {
        QMatrix4x4 toClip;      // meters around the land's middle to the picture's clip space
        double nearPlane{1.0};
        QVector3D target;       // the rider, in those meters
    };

    // The land taken over from the background, or made anew when it no longer will do.
    void takePatch(const View &view);
    Camera cameraFor(const QSize &size, const View &view) const;
    // The route over `image`; `hidden` says whether the land in front hides a point (pixels, 1 / w).
    void drawRoute(QPainter &painter, const QSize &size, const Camera &camera, const View &view,
                   const std::function<bool(const QPointF &, double)> &hidden) const;
    bool needsPatch(const View &view) const;
    // The land around `view`; reads nothing of the flyover, so it may be made in any thread.
    static Patch makePatch(const View &view);
    static std::shared_ptr<const Land> makeLand(const Patch &patch);
    // Meters east and north of the patch's middle.
    QPointF local(double latitude, double longitude) const;
    double groundAt(double east, double north) const;

    mutable QMutex m_mutex;
    QVector<Point> m_route;
    Patch m_patch;
    std::shared_ptr<Pending> m_pending{std::make_shared<Pending>()};
    QElapsedTimer m_built;      // since the patch was worked out: one with tiles missing waits a while
    bool m_lastComplete{true};
};
