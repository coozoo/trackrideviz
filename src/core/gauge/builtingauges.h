#pragma once

#include "gauge.h"

// Gauges that ship inside the application, with artwork drawn in code, so the
// program is usable without anybody else's gauge files.
// Needs a QGuiApplication (shapes are painted into images).
namespace BuiltinGauges
{
inline constexpr char DialId[] = "builtin:speedometer"; // the name projects already use
inline constexpr char ReadoutId[] = "builtin:readout";

QStringList ids();
// A gauge with no components when the id is not known.
Gauge byId(const QString &id);

// Round dial with a needle, numbered scale and a digital readout.
Gauge dial();
// Label, value and unit on a rounded plate.
Gauge readout();
}
