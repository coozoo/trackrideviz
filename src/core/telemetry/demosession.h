#pragma once

#include "telemetrysession.h"

// A made-up ten-minute ride on a loop, two laps, with every channel the
// built-in gauges know. For pictures of gauges and templates in the library,
// where the user's own recording may not be open yet.
namespace DemoSession
{
TelemetrySessionPtr create();
// A moment that shows the gauges mid-scale.
inline constexpr double ShowcaseTime = 205.0;
}
