#pragma once

#include "telemetrysession.h"

// Works out what a recording lacks from what it has, for every source alike:
// distance and heading from positions, speed from distance, grade, grade
// angle, total ascent and descent and vertical speed from elevation,
// G-force from speed and heading, and the small sums gauges ask for: running
// time, pace, power averaged over 3 and 30 seconds, the highest and lowest
// point, the length of the recording, braking, falling speed and glide ratio.
//
// Recorded channels are never replaced. Values are worked out on an even
// time grid (as fine as the positions, 0.1-1 s), not inside pauses, and
// smoothed: GPS positions and elevations are noisy, differences of them more
// so. The new channels are listed in TelemetrySession::derivedChannels().
namespace DerivedChannels
{
// A copy of `session` with the derived channels added; the session itself
// when there is nothing to add.
TelemetrySessionPtr complete(const TelemetrySessionPtr &session);
}
