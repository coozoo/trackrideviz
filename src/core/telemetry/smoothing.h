#pragma once

#include "channel.h"

// Gentler lines for charts: each sample replaced by the mean of the samples
// within `window` seconds around it. Only for showing; the data stays as
// recorded. A gap longer than the window (a pause) is not averaged across,
// and channels that hold their value (lap number, gear) are left as they are.
namespace Smoothing
{
Channel movingAverage(const Channel &channel, double window);
}
