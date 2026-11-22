#include "flyover.h"

#include "terrain.h"
#include "tilestore.h"

#include <QMatrix4x4>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QVector4D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace
{
const double kPi = 3.14159265358979323846;
const double kMetersPerDegree = 111320.0;
// The land worked out reaches this many spans each way; what is beyond lies in the haze.
const double kRadiusSpans = 2.5;
const int kCells = 128;
// It is worked out again when the rider is this far from its middle (of its radius).
const double kMoveShare = 0.25;
// The camera, as route videos have it: this many degrees from looking straight down, this
// high (in spans) above the rider, and seeing this much up and down (degrees).
const double kPitch = 62.0;
const double kAltitudeSpans = 0.45;
const double kFieldOfView = 45.0;
// the map's picture of the patch: this many pixels along its larger side at least
const double kTexturePixels = 2048.0;
// the sun, low in the north-west as on relief maps
const double kSunAzimuth = 315.0;
const double kSunElevation = 40.0;
// Hill shading: the slopes made this much steeper for the light alone, so that it shows hills
// the drawn heights keep modest; this strong, never darker than the ambient share; and only
// on slopes, fading out on flat land (by the slope's steepness to this power), which keeps
// the map's own colors there
const double kShadeSteepening = 2.5;
const double kShadeStrength = 0.8;
const double kShadeAmbient = 0.3;
const double kShadeFlatFade = 4.0;
// the haze grows with the distance to this power: the near land stays clear longer
const double kHazePower = 1.6;
const QColor kSkyTop(118, 163, 214);
const QColor kHaze(214, 226, 238);

struct Vertex
{
    double x{0.0};      // pixels
    double y{0.0};
    double inverseW{0.0};   // 1 / the distance along the view; 0: behind the camera
    double u{0.0};      // texture pixels, or the land's color without one
    double v{0.0};
    double w{0.0};
    double shade{1.0};
    bool usable{false};
};

inline double edge(const Vertex &a, const Vertex &b, double x, double y)
{
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

// The land's own color where there is no map: green low, brown higher, pale on the tops.
QColor landColor(double share)
{
    const QColor low(112, 142, 92);
    const QColor mid(150, 132, 100);
    const QColor high(205, 200, 192);
    auto mix = [](const QColor &a, const QColor &b, double f)
    {
        return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                                float(a.blueF() + (b.blueF() - a.blueF()) * f));
    };
    return share < 0.6 ? mix(low, mid, share / 0.6) : mix(mid, high, (share - 0.6) / 0.4);
}

inline QRgb sampleBilinear(const QRgb *texels, int stride, int width, int height, double u, double v)
{
    // in 1/256 of a pixel, from the pixel centers
    const int fu = std::clamp(int((u - 0.5) * 256.0), 0, (width - 1) * 256);
    const int fv = std::clamp(int((v - 0.5) * 256.0), 0, (height - 1) * 256);
    const int x0 = fu >> 8;
    const int y0 = fv >> 8;
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const quint32 fx = quint32(fu & 255);
    const quint32 fy = quint32(fv & 255);
    const QRgb *row0 = texels + y0 * stride;
    const QRgb *row1 = texels + y1 * stride;
    // two channels at a time: red and blue, then green (alpha is not needed)
    auto mix = [](quint32 a, quint32 b, quint32 f)
    {
        const quint32 rb = ((a & 0xff00ff) * (256 - f) + (b & 0xff00ff) * f) >> 8;
        const quint32 g = ((a & 0x00ff00) * (256 - f) + (b & 0x00ff00) * f) >> 8;
        return (rb & 0xff00ff) | (g & 0x00ff00);
    };
    return 0xff000000u | mix(mix(row0[x0], row0[x1], fx), mix(row1[x0], row1[x1], fx), fy);
}
}

void Flyover::setRoute(const QVector<Point> &route)
{
    const QMutexLocker lock(&m_mutex);
    m_route = route;
}

bool Flyover::wasComplete() const
{
    const QMutexLocker lock(&m_mutex);
    return m_lastComplete;
}

QPointF Flyover::local(double latitude, double longitude) const
{
    const double east = (longitude - m_patch.longitude) * kMetersPerDegree * std::cos(m_patch.latitude * kPi / 180.0);
    const double north = (latitude - m_patch.latitude) * kMetersPerDegree;
    return QPointF(east, north);
}

double Flyover::groundAt(double east, double north) const
{
    const int n = m_patch.cells;
    const double step = 2.0 * m_patch.radius / n;
    const double gx = std::clamp((east + m_patch.radius) / step, 0.0, double(n) - 1e-6);
    const double gy = std::clamp((north + m_patch.radius) / step, 0.0, double(n) - 1e-6);
    const int i = int(gx);
    const int j = int(gy);
    const double fx = gx - i;
    const double fy = gy - j;
    auto h = [&](int a, int b) { return double(m_patch.heights.at(b * (n + 1) + a)); };
    return (h(i, j) * (1.0 - fx) + h(i + 1, j) * fx) * (1.0 - fy) + (h(i, j + 1) * (1.0 - fx) + h(i + 1, j + 1) * fx) * fy;
}

bool Flyover::needsPatch(const View &view) const
{
    if (!m_patch.isValid() || !m_patch.complete || m_patch.mapProvider != view.mapProvider || m_patch.streets != view.streets
            || std::abs(m_patch.radius - view.span * kRadiusSpans) > m_patch.radius * 0.01)
        {
            return true;
        }
    const QPointF away = local(view.latitude, view.longitude);
    return std::hypot(away.x(), away.y()) > m_patch.radius * kMoveShare;
}

void Flyover::buildPatch(const View &view)
{
    Patch patch;
    patch.latitude = view.latitude;
    patch.longitude = view.longitude;
    patch.radius = view.span * kRadiusSpans;
    patch.cells = kCells;
    patch.mapProvider = view.mapProvider;
    patch.streets = view.streets;
    patch.complete = true;
    m_patch = patch;

    const int n = kCells;
    const double step = 2.0 * patch.radius / n;
    const double metersPerLongitude = kMetersPerDegree * std::cos(patch.latitude * kPi / 180.0);
    auto latitudeOf = [&](int j) { return patch.latitude + (-patch.radius + j * step) / kMetersPerDegree; };
    auto longitudeOf = [&](int i) { return patch.longitude + (-patch.radius + i * step) / metersPerLongitude; };

    // the heights; where a tile is not there yet, those around it stand in (and it is asked for)
    QVector<float> heights((n + 1) * (n + 1), std::numeric_limits<float>::quiet_NaN());
    double sum = 0.0;
    int known = 0;
    for (int j = 0; j <= n; ++j)
        {
            for (int i = 0; i <= n; ++i)
                {
                    double meters = 0.0;
                    if (Terrain::elevationAt(latitudeOf(j), longitudeOf(i), &meters))
                        {
                            heights[j * (n + 1) + i] = float(meters);
                            sum += meters;
                            ++known;
                        }
                    else
                        {
                            m_patch.complete = false;
                        }
                }
        }
    const float fallback = known > 0 ? float(sum / known) : 0.0f;
    for (float &h : heights)
        {
            if (std::isnan(h))
                {
                    h = fallback;
                }
        }
    m_patch.heights = heights;

    // the sun's light on each point, from its slope steepened for the light; on flat land none
    const double sunAzimuth = kSunAzimuth * kPi / 180.0;
    const double sunElevation = kSunElevation * kPi / 180.0;
    const QVector3D sun(float(std::cos(sunElevation) * std::sin(sunAzimuth)), float(std::cos(sunElevation) * std::cos(sunAzimuth)),
                        float(std::sin(sunElevation)));
    const double flat = std::sin(sunElevation);
    m_patch.shade.resize(heights.size());
    auto h = [&](int a, int b)
    {
        return double(heights.at(std::clamp(b, 0, n) * (n + 1) + std::clamp(a, 0, n))) * kShadeSteepening;
    };
    // the slope between the points `reach` cells either way
    auto normalAt = [&](int i, int j, int reach)
    {
        return QVector3D(float(-(h(i + reach, j) - h(i - reach, j)) / (2.0 * reach * step)),
                         float(-(h(i, j + reach) - h(i, j - reach)) / (2.0 * reach * step)), 1.0f).normalized();
    };
    for (int j = 0; j <= n; ++j)
        {
            for (int i = 0; i <= n; ++i)
                {
                    // towards the sun brighter than flat land, away from it darker
                    const double light = std::max(0.0, double(QVector3D::dotProduct(normalAt(i, j, 1), sun))) / flat;
                    double shading = kShadeAmbient + (1.0 - kShadeAmbient) * light;
                    shading = 1.0 + (shading - 1.0) * kShadeStrength;
                    // how steep the land is around it, wider than the light's slope: a bump on
                    // flat land stays unshaded
                    const double steepness = 1.0 - std::pow(std::max(0.0f, normalAt(i, j, 2).z()), kShadeFlatFade);
                    m_patch.shade[j * (n + 1) + i] = float(std::clamp(1.0 + (shading - 1.0) * steepness, 0.3, 1.4));
                }
        }

    // the map laid over it
    if (view.streets)
        {
            const double west = longitudeOf(0);
            const double east = longitudeOf(n);
            const double south = latitudeOf(0);
            const double north = latitudeOf(n);
            const QRectF area(QPointF(west, -TileStore::mercatorLatitude(north)), QPointF(east, -TileStore::mercatorLatitude(south)));
            const TileStore::Picture picture = TileStore::forMap(view.mapProvider)->picture(area, kTexturePixels);
            if (picture.image.isNull())
                {
                    m_patch.complete = false;
                }
            else
                {
                    m_patch.texture = picture.image.convertToFormat(QImage::Format_RGB32);
                    m_patch.textureBounds = picture.bounds;
                }
        }
}

QImage Flyover::render(const QSize &size, const View &view, QPointF *rider)
{
    const QMutexLocker lock(&m_mutex);
    QImage image(size, QImage::Format_RGB32);
    if (size.isEmpty())
        {
            return image;
        }
    if (needsPatch(view))
        {
            buildPatch(view);
        }
    m_lastComplete = m_patch.complete;
    const int width = size.width();
    const int height = size.height();

    // the sky, darker up high
    {
        QPainter painter(&image);
        QLinearGradient sky(0, 0, 0, height);
        sky.setColorAt(0.0, kSkyTop);
        sky.setColorAt(0.55, kHaze);
        painter.fillRect(image.rect(), sky);
    }

    // the camera: behind the rider and above, looking down at them
    const QPointF here = local(view.latitude, view.longitude);
    const double ground = groundAt(here.x(), here.y()) * view.exaggeration;
    const double heading = view.heading * kPi / 180.0;
    const QVector3D forward(float(std::sin(heading)), float(std::cos(heading)), 0.0f);
    const QVector3D target(float(here.x()), float(here.y()), float(ground));
    const double altitude = view.span * kAltitudeSpans;
    const double back = altitude * std::tan(kPitch * kPi / 180.0);
    const QVector3D eye = target - forward * float(back) + QVector3D(0.0f, 0.0f, float(altitude));
    const QVector3D lookAt = target + forward * float(view.span * 0.12);
    const double nearPlane = std::max(1.0, altitude * 0.05);
    const double farPlane = m_patch.radius * 2.0;
    QMatrix4x4 projection;
    projection.perspective(float(kFieldOfView), float(width) / height, float(nearPlane), float(farPlane));
    QMatrix4x4 camera;
    camera.lookAt(eye, lookAt, QVector3D(0.0f, 0.0f, 1.0f));
    const QMatrix4x4 toClip = projection * camera;
    auto project = [&](const QVector3D &point, Vertex *vertex)
    {
        const QVector4D clip = toClip * QVector4D(point, 1.0f);
        vertex->w = clip.w();
        vertex->usable = clip.w() > nearPlane;
        if (!vertex->usable)
            {
                return;
            }
        vertex->inverseW = 1.0 / clip.w();
        vertex->x = (clip.x() * vertex->inverseW + 1.0) * 0.5 * width;
        vertex->y = (1.0 - clip.y() * vertex->inverseW) * 0.5 * height;
    };

    // the land's points: where they are on the picture, their map pixel, their light
    const int n = m_patch.cells;
    const double step = 2.0 * m_patch.radius / n;
    const bool textured = !m_patch.texture.isNull();
    const double metersPerLongitude = kMetersPerDegree * std::cos(m_patch.latitude * kPi / 180.0);
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
    for (const float h : m_patch.heights)
        {
            lowest = std::min(lowest, h);
            highest = std::max(highest, h);
        }
    QVector<Vertex> vertices((n + 1) * (n + 1));
    QVector<QRgb> colors(textured ? 0 : vertices.size());
    for (int j = 0; j <= n; ++j)
        {
            for (int i = 0; i <= n; ++i)
                {
                    const int k = j * (n + 1) + i;
                    const double east = -m_patch.radius + i * step;
                    const double north = -m_patch.radius + j * step;
                    Vertex &vertex = vertices[k];
                    project(QVector3D(float(east), float(north), m_patch.heights.at(k) * float(view.exaggeration)), &vertex);
                    vertex.shade = m_patch.shade.at(k);
                    if (textured)
                        {
                            const double longitude = m_patch.longitude + east / metersPerLongitude;
                            const double latitude = m_patch.latitude + north / kMetersPerDegree;
                            const QRectF &bounds = m_patch.textureBounds;
                            vertex.u = (longitude - bounds.left()) / bounds.width() * m_patch.texture.width();
                            vertex.v = (-TileStore::mercatorLatitude(latitude) - bounds.top()) / bounds.height() * m_patch.texture.height();
                        }
                    else
                        {
                            const double share = highest > lowest ? (m_patch.heights.at(k) - lowest) / (highest - lowest) : 0.3;
                            colors[k] = landColor(share).rgb();
                        }
                }
        }

    // the triangles, nearest kept: a depth of 1 / w for each pixel, larger is nearer. In bands
    // of rows, one for each processor: each band draws every triangle's rows inside it
    QVector<float> depth(width * height, 0.0f);
    QRgb *pixels = reinterpret_cast<QRgb *>(image.bits());
    const int stride = image.bytesPerLine() / int(sizeof(QRgb));
    const QRgb *texels = textured ? reinterpret_cast<const QRgb *>(m_patch.texture.constBits()) : nullptr;
    const int texelStride = textured ? m_patch.texture.bytesPerLine() / int(sizeof(QRgb)) : 0;
    const int textureWidth = m_patch.texture.width();
    const int textureHeight = m_patch.texture.height();
    const double hazeFrom = m_patch.radius * 0.4;
    const double hazeTo = m_patch.radius * 0.95;
    auto triangle = [&](int ia, int ib, int ic, int bandTop, int bandBottom)
    {
        const Vertex &a = vertices.at(ia);
        const Vertex &b = vertices.at(ib);
        const Vertex &c = vertices.at(ic);
        if (!a.usable || !b.usable || !c.usable)
            {
                return;
            }
        const double area = edge(a, b, c.x, c.y);
        if (std::abs(area) < 1e-9)
            {
                return;
            }
        const int left = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int right = std::min(width - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int top = std::max(bandTop, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int bottom = std::min(bandBottom, int(std::ceil(std::max({a.y, b.y, c.y}))));
        if (left > right || top > bottom)
            {
                return;
            }
        // the weights of a, b and c: linear in x and y, so each pixel adds a step
        const double scale = 1.0 / area;
        const double stepA = -(c.y - b.y) * scale;
        const double stepB = -(a.y - c.y) * scale;
        // what is interpolated, divided by w for the perspective
        const double au = a.u * a.inverseW, bu = b.u * b.inverseW, cu = c.u * c.inverseW;
        const double av = a.v * a.inverseW, bv = b.v * b.inverseW, cv = c.v * c.inverseW;
        const double as = a.shade * a.inverseW, bs = b.shade * b.inverseW, cs = c.shade * c.inverseW;
        const QRgb ca = textured ? 0 : colors.at(ia);
        const QRgb cb = textured ? 0 : colors.at(ib);
        const QRgb cc = textured ? 0 : colors.at(ic);
        for (int y = top; y <= bottom; ++y)
            {
                const double py = y + 0.5;
                double wa = edge(b, c, left + 0.5, py) * scale;
                double wb = edge(c, a, left + 0.5, py) * scale;
                QRgb *row = pixels + y * stride;
                float *depthRow = depth.data() + y * width;
                for (int x = left; x <= right; ++x, wa += stepA, wb += stepB)
                    {
                        const double wc = 1.0 - wa - wb;
                        if (wa < 0.0 || wb < 0.0 || wc < 0.0)
                            {
                                continue;
                            }
                        const double inverseW = wa * a.inverseW + wb * b.inverseW + wc * c.inverseW;
                        if (inverseW <= depthRow[x])
                            {
                                continue;
                            }
                        depthRow[x] = float(inverseW);
                        const double toValue = 1.0 / inverseW;
                        const double shade = (wa * as + wb * bs + wc * cs) * toValue;
                        int r, g, bl;
                        if (textured)
                            {
                                const QRgb texel = sampleBilinear(texels, texelStride, textureWidth, textureHeight,
                                                                  (wa * au + wb * bu + wc * cu) * toValue,
                                                                  (wa * av + wb * bv + wc * cv) * toValue);
                                r = qRed(texel);
                                g = qGreen(texel);
                                bl = qBlue(texel);
                            }
                        else
                            {
                                r = int(wa * qRed(ca) + wb * qRed(cb) + wc * qRed(cc));
                                g = int(wa * qGreen(ca) + wb * qGreen(cb) + wc * qGreen(cc));
                                bl = int(wa * qBlue(ca) + wb * qBlue(cb) + wc * qBlue(cc));
                            }
                        const double distance = std::clamp((toValue - hazeFrom) / (hazeTo - hazeFrom), 0.0, 1.0);
                        const double haze = distance > 0.0 ? std::pow(distance, kHazePower) : 0.0;
                        const double keep = shade * (1.0 - haze);
                        row[x] = qRgb(std::clamp(int(r * keep + kHaze.red() * haze), 0, 255),
                                      std::clamp(int(g * keep + kHaze.green() * haze), 0, 255),
                                      std::clamp(int(bl * keep + kHaze.blue() * haze), 0, 255));
                    }
            }
    };
    const int bands = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    auto drawBand = [&](int band)
    {
        const int bandTop = height * band / bands;
        const int bandBottom = height * (band + 1) / bands - 1;
        for (int j = 0; j < n; ++j)
            {
                for (int i = 0; i < n; ++i)
                    {
                        const int k = j * (n + 1) + i;
                        triangle(k, k + 1, k + n + 2, bandTop, bandBottom);
                        triangle(k, k + n + 2, k + n + 1, bandTop, bandBottom);
                    }
            }
    };
    std::vector<std::thread> workers;
    for (int band = 1; band < bands; ++band)
        {
            workers.emplace_back(drawBand, band);
        }
    drawBand(0);
    for (std::thread &worker : workers)
        {
            worker.join();
        }

    // the route on the land where it shows: ridden in its color, the rest faint
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    auto visible = [&](const Vertex &vertex)
    {
        const int x = int(vertex.x);
        const int y = int(vertex.y);
        return vertex.usable && x >= 0 && y >= 0 && x < width && y < height && vertex.inverseW >= depth.at(y * width + x) * 0.985;
    };
    const double reach = m_patch.radius * 0.95;
    QPainterPath ridden;
    QPainterPath ahead;
    bool riddenOpen = false;
    bool aheadOpen = false;
    for (const Point &point : std::as_const(m_route))
        {
            const QPointF at = local(point.latitude, point.longitude);
            Vertex vertex;
            const bool near = std::abs(at.x()) < reach && std::abs(at.y()) < reach;
            if (near)
                {
                    // a little above the ground, so that the land does not swallow it
                    project(QVector3D(float(at.x()), float(at.y()),
                                      float(groundAt(at.x(), at.y()) * view.exaggeration + view.span * 0.004)), &vertex);
                }
            // not into the haze, where the land it lies on has faded
            const bool shown = near && visible(vertex) && vertex.w < (hazeFrom + hazeTo) / 2.0;
            const bool isRidden = point.time <= view.time;
            QPainterPath &path = isRidden ? ridden : ahead;
            bool &open = isRidden ? riddenOpen : aheadOpen;
            if (!shown)
                {
                    riddenOpen = aheadOpen = false;
                    continue;
                }
            if (open)
                {
                    path.lineTo(vertex.x, vertex.y);
                }
            else
                {
                    path.moveTo(vertex.x, vertex.y);
                    open = true;
                }
            // the other part starts where this one is, so that the two meet
            (isRidden ? aheadOpen : riddenOpen) = false;
        }
    painter.setBrush(Qt::NoBrush);
    QColor faint = view.line;
    faint.setAlpha(110);
    painter.setPen(QPen(faint, view.lineWidth * 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(ahead);
    painter.setPen(QPen(view.line, view.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(ridden);
    painter.end();

    if (rider)
        {
            Vertex vertex;
            project(QVector3D(target.x(), target.y(), target.z() + float(view.span * 0.004)), &vertex);
            *rider = vertex.usable ? QPointF(vertex.x, vertex.y) : QPointF(width / 2.0, height * 0.6);
        }
    return image;
}

void Flyover::want(const QVector<Point> &places, double span, const QString &mapProvider, bool streets)
{
    const double radius = span * kRadiusSpans;
    for (const Point &place : places)
        {
            const double metersPerLongitude = kMetersPerDegree * std::cos(place.latitude * kPi / 180.0);
            const double dLat = radius / kMetersPerDegree;
            const double dLon = radius / metersPerLongitude;
            // the heights' tiles: those under the corners, the edges and the middle
            for (int j = -2; j <= 2; ++j)
                {
                    for (int i = -2; i <= 2; ++i)
                        {
                            double meters = 0.0;
                            Terrain::elevationAt(place.latitude + dLat * j / 2.0, place.longitude + dLon * i / 2.0, &meters);
                        }
                }
            if (streets)
                {
                    const QRectF area(QPointF(place.longitude - dLon, -TileStore::mercatorLatitude(place.latitude + dLat)),
                                      QPointF(place.longitude + dLon, -TileStore::mercatorLatitude(place.latitude - dLat)));
                    TileStore::forMap(mapProvider)->want({area}, kTexturePixels);
                }
        }
}
