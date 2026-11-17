#pragma once

#include "gauge.h"

// A gauge laid out again for another width, the height kept: what is placed
// across it (frames, points, pivots, the travel of bars and tapes) is spread
// over the new width, while letters keep their size and what is drawn at the
// current point (a graph's dot, a map's arrow) its shape. Graphs, profiles,
// bars and tapes then fill the width; round parts become oval.
namespace GaugeShape
{
// `aspect`: width / height wanted. The gauge as it is for 0 or its own shape.
Gauge widened(const Gauge &gauge, double aspect);
}
