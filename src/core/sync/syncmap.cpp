#include "syncmap.h"

#include <algorithm>

double SyncMap::shiftAt(double videoTime) const
{
    double shift = 0.0;
    for (const TimeShift &step : shifts)
        {
            if (videoTime < step.at)
                {
                    break;
                }
            shift = step.shift;
        }
    return shift;
}

double SyncMap::videoTime(double telemetryTime) const
{
    // part by part: [start, next start) of video shows [start + offset + shift, ...)
    double start = -1e300;
    double shift = 0.0;
    for (const TimeShift &step : shifts)
        {
            const double t = telemetryTime - offset - shift;
            if (t < step.at)
                {
                    return std::max(t, start);
                }
            start = step.at;
            shift = step.shift;
        }
    return std::max(telemetryTime - offset - shift, start);
}
