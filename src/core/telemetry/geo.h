#pragma once

#include <QtMath>

namespace Geo
{
// Great-circle distance in metres between two WGS84 points given in degrees.
inline double distanceMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double earthRadius = 6371008.8;
    const double p1 = qDegreesToRadians(lat1);
    const double p2 = qDegreesToRadians(lat2);
    const double dp = p2 - p1;
    const double dl = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dp / 2) * qSin(dp / 2) + qCos(p1) * qCos(p2) * qSin(dl / 2) * qSin(dl / 2);
    return earthRadius * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}
}
