#include "smoothing.h"

#include <QVector>

Channel Smoothing::movingAverage(const Channel &channel, double window)
{
    if (window <= 0.0 || channel.interpolation() == Channel::Step || channel.count() < 3)
        {
            return channel;
        }
    Channel smooth(channel.id(), channel.unit(), channel.interpolation());
    const int count = channel.count();
    const double half = window / 2.0;
    int segmentStart = 0;
    while (segmentStart < count)
        {
            // up to the next gap longer than the window: a pause is not averaged across
            int segmentEnd = segmentStart + 1;
            while (segmentEnd < count && channel.timeAt(segmentEnd) - channel.timeAt(segmentEnd - 1) <= window)
                {
                    ++segmentEnd;
                }
            // a running sum over the samples within half the window either side
            int first = segmentStart;
            int next = segmentStart;
            double sum = 0.0;
            for (int i = segmentStart; i < segmentEnd; ++i)
                {
                    const double t = channel.timeAt(i);
                    while (next < segmentEnd && channel.timeAt(next) <= t + half)
                        {
                            sum += channel.valueAtIndex(next++);
                        }
                    while (channel.timeAt(first) < t - half)
                        {
                            sum -= channel.valueAtIndex(first++);
                        }
                    smooth.append(t, sum / (next - first));
                }
            segmentStart = segmentEnd;
        }
    return smooth;
}
