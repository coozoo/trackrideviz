#pragma once

#include "gauge.h"

// Gauges that ship inside the application, with artwork drawn in code, so the
// program is usable without anybody else's gauge files.
// Needs a QGuiApplication (text is drawn into the dial image).
namespace BuiltinGauges
{
// Round dial 0..maxKmh with a needle and a digital readout, bound to the speed channel.
Gauge speedometer(int maxKmh = 80);
}
