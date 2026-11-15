#pragma once

#include "telemetrysession.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

// The bike's gears: its chainrings, its sprockets and how far a turn of the
// wheel goes. Filled in once in the settings (the Bike page).
struct Drivetrain
{
    QVector<int> chainrings;   // teeth, smallest first
    QVector<int> cassette;     // teeth, smallest first
    QString tireSize;          // as printed on the tire: "700x25c", "29x2.25", "40-622"
    double wheelMm{0.0};       // the wheel's circumference; 0 = from the tire size

    bool isEmpty() const { return chainrings.isEmpty() || cassette.isEmpty(); }
    // The circumference used: the one entered, or the tire size's, or that of a usual road wheel.
    double circumferenceMm() const;

    bool operator==(const Drivetrain &other) const
    {
        return chainrings == other.chainrings && cassette == other.cassette && tireSize == other.tireSize
               && wheelMm == other.wheelMm;
    }
    bool operator!=(const Drivetrain &other) const { return !(*this == other); }
};

// The gear a ride was in, moment by moment (channelids.h): the chainring's and
// the sprocket's teeth, their numbers (the rear's counted from the largest
// sprocket, the easiest gear being 1; the front's from the smallest ring) and
// the ratio between them.
//
// Electronic shifting records the gears; the FIT importer reads them and they
// are kept. Otherwise they are worked out from the speed and the cadence: the
// distance a turn of the cranks went, against what each gear gives, over the
// whole ride at once: the gears that explain the seconds best with the fewest
// shifts, a chainring change costing much more than a sprocket, and a crossed
// chain a little. Where the cranks are still or turn slower than the wheel
// (coasting, soft pedaling downhill) the seconds tell little and the gear stays.
class Gears
{
    Q_DECLARE_TR_FUNCTIONS(Gears)

public:
    // Teeth from "50, 34" or "11 13 15 17": smallest first; numbers that are no teeth are left out.
    static QVector<int> parseTeeth(const QString &text);
    static QString teethText(const QVector<int> &teeth);
    // The circumference of a wheel with this tire, mm; 0 when the size cannot be read.
    static double circumferenceOf(const QString &tireSize);

    struct Preset
    {
        QString name;
        QVector<int> teeth;
    };
    // Usual chainrings and cassettes, to start from.
    static QVector<Preset> chainringPresets();
    static QVector<Preset> cassettePresets();

    // A copy of `session` with the gear channels; the session itself when there is nothing to add.
    // Wants the speed in place (DerivedChannels::complete).
    static TelemetrySessionPtr apply(const TelemetrySessionPtr &session, const Drivetrain &drivetrain);
};
