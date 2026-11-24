#pragma once

#include "../telemetry/telemetrysession.h"

#include <QHash>
#include <QImage>

// Heights of the ground from a terrain model, for recordings without an
// elevation or with a poor one.
//
// The model comes as tiles like a map's, with the height in each pixel's
// color ("terrarium" tiles: about 25 m to a pixel here, from SRTM and
// national surveys), fetched and kept by TileStore::terrain().
namespace Terrain
{
// When the terrain's heights become the recording's elevation.
enum Use
{
    WhenMissing,   // only for a recording that has none
    Always,        // in place of the recorded one
    Never
};

// The tiles looked at for many heights in one go (the land of a 3D flyover is thousands):
// each is asked of the store once. Kept for that one go only, a tile that comes is seen the next.
struct Lookup
{
    QHash<quint64, QImage> tiles;   // null: not there
};

// The height at a position. False when the tile is not on disk; it has then
// been asked for (TileStore::terrain() says when it has come).
bool elevationAt(double latitude, double longitude, double *meters, Lookup *lookup = nullptr);

// The session with its elevation taken from the terrain along its positions,
// as `use` says; the session itself when nothing changes. Elevation, grade and
// the other channels a recording made from its own elevation go when it is
// replaced: they are worked out again from the new one. With tiles still
// missing nothing is changed and *waiting is set.
TelemetrySessionPtr apply(const TelemetrySessionPtr &session, Use use, bool *waiting = nullptr);

// The credit the makers of the model ask for.
QString attribution();
}
