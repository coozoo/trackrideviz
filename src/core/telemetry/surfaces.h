#pragma once

#include <QCoreApplication>
#include <QString>
#include <QVector>

// What the wheels roll on. A kind of surface is a name and how much harder
// than typical asphalt it rolls; a stretch of a ride is marked as one of them
// and the power estimate counts it in.
struct SurfaceType
{
    QString name;
    double factor{1.0};   // times the tire's rolling resistance on typical asphalt

    bool operator==(const SurfaceType &other) const { return name == other.name && factor == other.factor; }
};

// A stretch of a ride on one surface. It keeps the name and the factor of
// its kind as they were when it was marked: a project reads the same on a
// machine with other kinds.
struct SurfaceStretch
{
    double from{0.0};   // m along the ride
    double to{0.0};
    QString surface;
    double factor{1.0};

    bool operator==(const SurfaceStretch &other) const
    {
        return from == other.from && to == other.to && surface == other.surface && factor == other.factor;
    }
};

class Surfaces
{
    Q_DECLARE_TR_FUNCTIONS(Surfaces)

public:
    // The kinds the program starts with.
    static QVector<SurfaceType> usual();
    // What the surface `distance` meters into the ride multiplies the rolling resistance by:
    // 1 where nothing is marked; where stretches overlap, the one marked last.
    static double factorAt(const QVector<SurfaceStretch> &stretches, double distance);
};
