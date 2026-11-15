#include "surfaces.h"

QVector<SurfaceType> Surfaces::usual()
{
    return
    {
        {tr("Smooth asphalt"), 0.8},
        {tr("Typical asphalt"), 1.0},
        {tr("Rough asphalt"), 1.3},
        {tr("Paving stones"), 1.6},
        {tr("Hard gravel"), 1.8},
        {tr("Loose gravel"), 2.5},
        {tr("Dirt track"), 2.2},
        {tr("Grass"), 3.5},
        {tr("Sand, mud"), 5.0},
    };
}

double Surfaces::factorAt(const QVector<SurfaceStretch> &stretches, double distance)
{
    for (auto it = stretches.crbegin(); it != stretches.crend(); ++it)
        {
            if (distance >= it->from && distance <= it->to)
                {
                    return it->factor;
                }
        }
    return 1.0;
}
