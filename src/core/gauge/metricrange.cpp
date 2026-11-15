#include "metricrange.h"

#include <algorithm>
#include <cmath>

namespace
{
// Smallest of 1, 2, 2.5, 5 x 10^k that is not below `raw`.
double niceStep(double raw)
{
    if (!(raw > 0.0) || !std::isfinite(raw))
        {
            return 1.0;
        }
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (double factor : {1.0, 2.0, 2.5, 5.0, 10.0})
        {
            // the tolerance keeps 0.30000000000000004 from skipping 0.3's own step
            if (factor * magnitude >= raw * (1.0 - 1e-9))
                {
                    return factor * magnitude;
                }
        }
    return 10.0 * magnitude;
}
}

MetricRange MetricRange::covering(double dataMin, double dataMax, int markings)
{
    const int intervals = markings >= 2 ? markings - 1 : 4;
    if (!std::isfinite(dataMin) || !std::isfinite(dataMax) || dataMax < dataMin)
        {
            dataMin = 0.0;
            dataMax = 100.0;
        }
    // Altitude around 2000 m on a dial from zero would never move the needle.
    const bool farFromZero = dataMin > 0.0 && dataMin > 0.6 * dataMax;
    const bool anchoredAtZero = dataMin >= 0.0 && !farFromZero;

    MetricRange range;
    double low = anchoredAtZero ? 0.0 : dataMin;
    range.step = niceStep((dataMax - low) / intervals);
    // rounding the start down to a step can push the end below the data: widen until it fits
    for (int attempt = 0; attempt < 8; ++attempt)
        {
            range.min = anchoredAtZero ? 0.0 : std::floor(low / range.step + 1e-9) * range.step;
            range.max = range.min + range.step * intervals;
            if (range.max >= dataMax - range.step * 1e-9)
                {
                    break;
                }
            range.step = niceStep(range.step * 1.01);
        }
    return range;
}

MetricRange MetricRange::upTo(double min, double max, int intervals)
{
    MetricRange range;
    range.min = min;
    range.max = max > min ? max : min + 1.0;
    intervals = std::max(1, intervals);
    const double span = range.max - range.min;
    // the asked count first, then one more, one less, two more...
    for (int delta = 0; delta <= intervals; ++delta)
        {
            for (int count : {intervals + delta, intervals - delta})
                {
                    if (count < 2 || count > 2 * intervals + 1)
                        {
                            continue;
                        }
                    const double step = span / count;
                    // an end of one's own may also be counted in 15s, 30s, 40s
                    const double mantissa = step / std::pow(10.0, std::floor(std::log10(step)));
                    bool round = false;
                    for (double factor : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 10.0})
                        {
                            round = round || std::fabs(mantissa - factor) <= 1e-6;
                        }
                    if (round)
                        {
                            range.step = step;
                            return range;
                        }
                }
        }
    range.step = span / intervals;
    return range;
}

QStringList MetricRange::markingTexts(int markings) const
{
    // as many decimals as the step needs: 2.5 -> 1, 0.25 -> 2, 20 -> 0
    int decimals = 0;
    for (double scaled = step; decimals < 3 && std::fabs(scaled - std::round(scaled)) > 1e-6; scaled *= 10.0)
        {
            ++decimals;
        }
    // a step that is no round number (an end chosen by hand, 100 in 6 steps): as many as can be read
    decimals = std::min(decimals, step >= 10.0 ? 0 : step >= 1.0 ? 1 : 3);
    QStringList texts;
    for (int i = 0; i < markings; ++i)
        {
            texts << QString::number(min + step * i, 'f', decimals);
        }
    return texts;
}
