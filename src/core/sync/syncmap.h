#pragma once

#include <QVector>

// From video time `at` on, the data is `shift` seconds later than the offset
// alone says: a joined video whose recordings had a break between them.
struct TimeShift
{
    double at{0.0};
    double shift{0.0};

    bool operator==(const TimeShift &other) const { return at == other.at && shift == other.shift; }
};

// Where the video sits on the data's timeline:
//     telemetry time = video time + offset + the shift of the part it is in
// One video recorded in one go has no shifts.
struct SyncMap
{
    SyncMap() = default;
    SyncMap(double offset, const QVector<TimeShift> &shifts = {})
        : offset(offset)
        , shifts(shifts)
    {
    }

    double offset{0.0};
    QVector<TimeShift> shifts;    // in order of `at`

    double shiftAt(double videoTime) const;
    double telemetryTime(double videoTime) const { return videoTime + offset + shiftAt(videoTime); }
    // The video time showing telemetry time `t`. Data from a break between
    // two parts has no picture: the start of the part after it is given.
    double videoTime(double telemetryTime) const;
};
