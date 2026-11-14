#include "terrain.h"

#include "../telemetry/channelids.h"
#include "tilestore.h"

#include <QImage>

#include <algorithm>
#include <cmath>

namespace
{
// about 25 m to a pixel in the middle latitudes: as fine as the model behind the tiles
const int kZoom = 12;
const int kTileSize = 256;

// The height in one pixel of the world at kZoom; false without its tile.
bool pixelHeight(qint64 x, qint64 y, double *meters)
{
    const qint64 world = qint64(kTileSize) << kZoom;
    x = ((x % world) + world) % world;      // round the date line
    y = std::clamp<qint64>(y, 0, world - 1);
    const QImage tile = TileStore::terrain()->tile(kZoom, int(x / kTileSize), int(y / kTileSize));
    if (tile.isNull())
        {
            return false;
        }
    // red and green count whole meters from 32768 below the sea, blue the parts of one
    const QRgb pixel = tile.pixel(int(x % kTileSize) * tile.width() / kTileSize, int(y % kTileSize) * tile.height() / kTileSize);
    *meters = qRed(pixel) * 256.0 + qGreen(pixel) + qBlue(pixel) / 256.0 - 32768.0;
    return true;
}
}

bool Terrain::elevationAt(double latitude, double longitude, double *meters)
{
    const double world = double(qint64(kTileSize) << kZoom);
    // pixel centers are at half numbers
    const double px = (longitude + 180.0) / 360.0 * world - 0.5;
    const double py = (180.0 - TileStore::mercatorLatitude(latitude)) / 360.0 * world - 0.5;
    const qint64 x0 = qint64(std::floor(px));
    const qint64 y0 = qint64(std::floor(py));
    const double fx = px - x0;
    const double fy = py - y0;
    // between the four pixels around; all four are looked at, so that all their tiles are asked for
    double h[4] = {0.0, 0.0, 0.0, 0.0};
    bool all = true;
    for (int i = 0; i < 4; ++i)
        {
            all = pixelHeight(x0 + i % 2, y0 + i / 2, &h[i]) && all;
        }
    if (!all)
        {
            return false;
        }
    *meters = (h[0] * (1.0 - fx) + h[1] * fx) * (1.0 - fy) + (h[2] * (1.0 - fx) + h[3] * fx) * fy;
    return true;
}

TelemetrySessionPtr Terrain::apply(const TelemetrySessionPtr &session, Use use, bool *waiting)
{
    if (waiting)
        {
            *waiting = false;
        }
    if (!session || use == Never)
        {
            return session;
        }
    const Channel *lat = session->channel(ChannelId::PositionLat);
    const Channel *lon = session->channel(ChannelId::PositionLon);
    const Channel *recorded = session->channel(ChannelId::Elevation);
    const bool has = recorded && !recorded->isEmpty();
    if (!lat || !lon || lat->count() < 2 || (has && use == WhenMissing))
        {
            return session;
        }
    QVector<double> times;
    QVector<double> heights;
    bool missing = false;
    for (int i = 0; i < lat->count(); ++i)
        {
            const double t = lat->timeAt(i);
            bool ok = false;
            const double longitude = lon->valueAt(t, &ok);
            double height = 0.0;
            if (!ok)
                {
                    continue;
                }
            // to the end even with a tile missing: every tile of the way gets asked for
            if (Terrain::elevationAt(lat->valueAtIndex(i), longitude, &height))
                {
                    times.append(t);
                    heights.append(height);
                }
            else
                {
                    missing = true;
                }
        }
    if (missing || times.size() < 2)
        {
            if (waiting)
                {
                    *waiting = missing;
                }
            return session;
        }
    // the model is of the ground in 25 m squares, not of the road cut into it: evened out
    // over a few seconds either side, or every square's edge would be a step
    QVector<double> even(heights.size());
    for (int i = 0; i < heights.size(); ++i)
        {
            double sum = 0.0;
            int count = 0;
            for (int k = i; k >= 0 && times.at(i) - times.at(k) <= 4.0; --k)
                {
                    sum += heights.at(k);
                    ++count;
                }
            for (int k = i + 1; k < heights.size() && times.at(k) - times.at(i) <= 4.0; ++k)
                {
                    sum += heights.at(k);
                    ++count;
                }
            even[i] = sum / count;
        }
    auto changed = std::make_shared<TelemetrySession>(*session);
    // what the recording made of its own elevation goes with it
    for (const char *id : {ChannelId::Elevation, ChannelId::Grade, ChannelId::GradeAngle, ChannelId::VerticalSpeed,
                           ChannelId::TotalAscent, ChannelId::TotalDescent})
        {
            changed->removeChannel(id);
        }
    Channel &elevation = changed->channelForWriting(ChannelId::Elevation, "m");
    for (int i = 0; i < times.size(); ++i)
        {
            elevation.append(times.at(i), even.at(i));
        }
    changed->addDerivedChannel(ChannelId::Elevation);
    return changed;
}

QString Terrain::attribution()
{
    return QStringLiteral("Terrain Tiles (Mapzen, AWS Open Data): SRTM, GMTED, NED, ETOPO1 and others");
}
