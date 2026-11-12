#include "metricrange.h"

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

QStringList MetricRange::markingTexts(int markings) const
{
    // as many decimals as the step needs: 2.5 -> 1, 0.25 -> 2, 20 -> 0
    int decimals = 0;
    for (double scaled = step; decimals < 3 && std::fabs(scaled - std::round(scaled)) > 1e-6; scaled *= 10.0)
        {
            ++decimals;
        }
    QStringList texts;
    for (int i = 0; i < markings; ++i)
        {
            texts << QString::number(min + step * i, 'f', decimals);
        }
    return texts;
}
