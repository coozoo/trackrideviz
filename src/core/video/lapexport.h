#pragma once

#include "cutlist.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

// One video per lap: where each lap is in the video, which laps are wanted, what they are called.
namespace LapExport
{
// The finished laps of `session` as stretches of the video, placed by `sync`, within
// 0..videoDuration; lap n at n - 1. A lap the video does not show at all is an empty range.
QVector<TimeRange> ranges(const TelemetrySession &session, const SyncMap &sync, double videoDuration);

// Lap numbers from 1 out of "all" (or nothing), "3" or "1, 4-6", each at most `count`, in order
// and once. False (with `*error`) for anything else.
bool numbers(const QString &text, int count, QVector<int> *laps, QString *error = nullptr);

// `path` with "_lapNN" before its suffix, as wide as the number of the last of `count` laps.
QString outputPath(const QString &path, int lap, int count);
}
