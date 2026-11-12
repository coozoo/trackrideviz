#pragma once

#include "gauge.h"

#include "../units/units.h"

// Gauges that ship inside the application, with artwork drawn in code, so the
// program is usable without anybody else's gauge files.
// Needs a QGuiApplication (text is drawn into the dial image).
namespace BuiltinGauges
{
// Round dial with a needle and a digital readout, bound to the speed channel:
// 0..80 km/h or 0..50 mph, whichever the units ask for.
Gauge speedometer(const Units &units = Units());

// Ids under which projects refer to the built-in gauges.
inline constexpr char SpeedometerId[] = "builtin:speedometer";
// A gauge with no components when the id is not known.
Gauge byId(const QString &id, const Units &units);
}
