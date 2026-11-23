#pragma once

#include "dataedits.h"
#include "telemetrysession.h"

#include <QVector>

// Where the positions of a recording go wrong, found from the positions themselves:
// - a gap: no position for a while (a tunnel, the GPS off) while the rider went on, not a stop
//   or a pause of the recording;
// - a jump: positions far off the way, farther than the speed allows, and back;
// - frozen: the position stands still while a speed sensor says the rider moves.
// A jump and a frozen stretch are fixed by taking their positions out (a DataEdit, which can be
// put back), so the track is read straight across; a gap already is.
namespace GpsProblems
{
struct Problem
{
    enum Kind { Gap, Jump, Frozen };
    Kind kind{Gap};
    double start{0.0};      // data time of the first sample concerned
    double end{0.0};        // ... and of the last
    double meters{0.0};     // gap: straight across it; jump: how far off the way it went
    bool fixable() const { return kind != Gap; }
    // the correction that fixes it (fixable ones only)
    DataEdit fix() const;

    bool operator==(const Problem &other) const
    {
        return kind == other.kind && start == other.start && end == other.end;
    }
};

// In order of time. `session`: the data as corrected by `edits` (what is fixed is not found again,
// nor the gap its fix leaves).
QVector<Problem> find(const TelemetrySession &session, const QVector<DataEdit> &edits = {});
}
