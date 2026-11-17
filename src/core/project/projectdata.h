#pragma once

#include "../maps/terrain.h"
#include "../telemetry/dataedits.h"
#include "../telemetry/gears.h"
#include "../telemetry/powerestimate.h"
#include "../telemetry/surfaces.h"
#include "../telemetry/telemetrysession.h"
#include "../weather/weather.h"

// What gauges read: the file's data corrected, with the terrain's heights where the project
// asks for them, and everything that can be worked out from that. The window and the command
// line both work it out this way.
namespace ProjectData
{
// `waiting`: heights of the terrain are still being fetched; `weatherWaiting`: the weather of
// the ride. What comes later is had by working it out again.
TelemetrySessionPtr workedOut(const TelemetrySessionPtr &raw, const QVector<DataEdit> &edits, Terrain::Use elevation,
                              const PowerEstimate::Rider &rider, const QVector<SurfaceStretch> &surfaces,
                              const WeatherChoice &weatherChoice, const HandWind &handWind, const Drivetrain &drivetrain,
                              bool *waiting, bool *weatherWaiting);
}
