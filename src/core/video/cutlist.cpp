#include "cutlist.h"

#include "../telemetry/channelids.h"

#include <algorithm>
#include <limits>

QVector<TimeRange> CutList::normalized(const QVector<TimeRange> &cuts, double duration)
{
    QVector<TimeRange> sorted;
    for (TimeRange cut : cuts)
        {
            cut.start = std::max(0.0, cut.start);
            if (duration > 0.0)
                {
                    cut.end = std::min(duration, cut.end);
                }
            if (cut.end > cut.start)
                {
                    sorted.append(cut);
                }
        }
    std::sort(sorted.begin(), sorted.end(), [](const TimeRange &a, const TimeRange &b) { return a.start < b.start; });
    QVector<TimeRange> joined;
    for (const TimeRange &cut : sorted)
        {
            if (!joined.isEmpty() && cut.start <= joined.last().end)
                {
                    joined.last().end = std::max(joined.last().end, cut.end);
                }
            else
                {
                    joined.append(cut);
                }
        }
    return joined;
}

QVector<TimeRange> CutList::added(const QVector<TimeRange> &cuts, const TimeRange &range)
{
    return normalized(cuts + QVector<TimeRange>{range});
}

QVector<TimeRange> CutList::removed(const QVector<TimeRange> &cuts, const TimeRange &range)
{
    QVector<TimeRange> left;
    for (const TimeRange &cut : normalized(cuts))
        {
            if (cut.end <= range.start || cut.start >= range.end)
                {
                    left.append(cut);
                    continue;
                }
            if (cut.start < range.start)
                {
                    left.append({cut.start, range.start});
                }
            if (cut.end > range.end)
                {
                    left.append({range.end, cut.end});
                }
        }
    return left;
}

QVector<TimeRange> CutList::kept(const QVector<TimeRange> &cuts, double from, double to)
{
    QVector<TimeRange> parts;
    double at = from;
    for (const TimeRange &cut : normalized(cuts))
        {
            if (cut.end <= at)
                {
                    continue;
                }
            if (cut.start >= to)
                {
                    break;
                }
            if (cut.start > at)
                {
                    parts.append({at, cut.start});
                }
            at = cut.end;
        }
    if (to > at)
        {
            parts.append({at, to});
        }
    return parts;
}

double CutList::keptLength(const QVector<TimeRange> &kept)
{
    double length = 0.0;
    for (const TimeRange &part : kept)
        {
            length += part.length();
        }
    return length;
}

double CutList::sourceTime(const QVector<TimeRange> &kept, double outputTime)
{
    for (const TimeRange &part : kept)
        {
            if (outputTime < part.length())
                {
                    return part.start + outputTime;
                }
            outputTime -= part.length();
        }
    return kept.isEmpty() ? outputTime : kept.last().end + outputTime;
}

const TimeRange *CutList::cutAt(const QVector<TimeRange> &cuts, double t)
{
    for (const TimeRange &cut : cuts)
        {
            if (cut.contains(t))
                {
                    return &cut;
                }
        }
    return nullptr;
}

QVector<TimeRange> CutList::stops(const TelemetrySession &session, const SyncMap &sync, double videoDuration,
                                  double minimum, double maxSpeed, double margin)
{
    // in telemetry time first
    QVector<TimeRange> still;
    const Channel *speed = session.channel(ChannelId::Speed);
    if (speed)
        {
            double slowSince = -1.0;
            for (int i = 0; i < speed->count(); ++i)
                {
                    const double t = speed->timeAt(i);
                    if (speed->valueAtIndex(i) < maxSpeed)
                        {
                            if (slowSince < 0.0)
                                {
                                    slowSince = t;
                                }
                        }
                    else if (slowSince >= 0.0)
                        {
                            still.append({slowSince, t});
                            slowSince = -1.0;
                        }
                }
            if (slowSince >= 0.0)
                {
                    still.append({slowSince, speed->timeAt(speed->count() - 1)});
                }
        }
    // the device stopped recording while standing (auto-pause)
    for (const TelemetrySession::Pause &pause : session.pauses())
        {
            still.append({pause.startTime, pause.endTime});
        }
    QVector<TimeRange> cuts;
    for (const TimeRange &range : normalized(still))
        {
            if (range.length() < minimum || range.length() <= 2.0 * margin)
                {
                    continue;
                }
            const TimeRange cut{sync.videoTime(range.start + margin), sync.videoTime(range.end - margin)};
            if (cut.length() > 0.0)
                {
                    cuts.append(cut);
                }
        }
    return normalized(cuts, videoDuration > 0.0 ? videoDuration : std::numeric_limits<double>::max());
}
