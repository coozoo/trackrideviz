#include "channel.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <deque>

Channel::Channel(const QString &id, const QString &unit, Interpolation interpolation)
    : m_id(id), m_unit(unit), m_interpolation(interpolation)
{
}

double Channel::startTime() const
{
    return m_times.isEmpty() ? 0.0 : m_times.first();
}

double Channel::endTime() const
{
    return m_times.isEmpty() ? 0.0 : m_times.last();
}

double Channel::minValue() const
{
    return m_values.isEmpty() ? 0.0 : *std::min_element(m_values.cbegin(), m_values.cend());
}

double Channel::steadyMax(double seconds) const
{
    // The lowest value over each stretch of `seconds`, read as the channel is between its
    // readings, and the highest of those: a lone reading is not held, nor the last few as the
    // logger stops. Only stretches inside the recording count, each starting or ending at a
    // reading. The lowest of the readings in each are kept rising (a sliding minimum), so the
    // work grows with the readings, not with how many are in a stretch.
    const int n = int(m_times.size());
    if (n < 2 || !(seconds > 0.0) || m_times.last() - m_times.first() < seconds)
        {
            return maxValue();
        }
    double best = -std::numeric_limits<double>::infinity();
    std::deque<int> lows;   // indices in the stretch, their values rising
    auto take = [&](int k)
    {
        while (!lows.empty() && m_values.at(lows.back()) >= m_values.at(k))
            {
                lows.pop_back();
            }
        lows.push_back(k);
    };
    // starting at each reading, forwards
    int next = 0;
    for (int i = 0; i < n && m_times.at(i) + seconds <= m_times.last(); ++i)
        {
            while (next < n && m_times.at(next) <= m_times.at(i) + seconds)
                {
                    take(next++);
                }
            while (lows.front() < i)
                {
                    lows.pop_front();
                }
            best = std::max(best, std::min(m_values.at(lows.front()), valueAt(m_times.at(i) + seconds)));
        }
    // ending at each, backwards
    lows.clear();
    next = n - 1;
    for (int i = n - 1; i >= 0 && m_times.at(i) - seconds >= m_times.first(); --i)
        {
            while (next >= 0 && m_times.at(next) >= m_times.at(i) - seconds)
                {
                    take(next--);
                }
            while (lows.front() > i)
                {
                    lows.pop_front();
                }
            best = std::max(best, std::min(m_values.at(lows.front()), valueAt(m_times.at(i) - seconds)));
        }
    return best;
}

double Channel::maxValue() const
{
    return m_values.isEmpty() ? 0.0 : *std::max_element(m_values.cbegin(), m_values.cend());
}

bool Channel::append(double timeSec, double value)
{
    if (!m_times.isEmpty() && timeSec <= m_times.last())
        {
            return false;
        }
    m_times.append(timeSec);
    m_values.append(value);
    return true;
}

int Channel::removeBetween(double from, double to)
{
    const int first = int(std::lower_bound(m_times.cbegin(), m_times.cend(), from) - m_times.cbegin());
    const int last = int(std::upper_bound(m_times.cbegin(), m_times.cend(), to) - m_times.cbegin());
    if (last <= first)
        {
            return 0;
        }
    m_times.remove(first, last - first);
    m_values.remove(first, last - first);
    return last - first;
}

double Channel::valueAt(double timeSec, bool *ok) const
{
    if (ok)
        {
            *ok = !m_times.isEmpty() && timeSec >= m_times.first() && timeSec <= m_times.last();
        }
    // no time (NaN) passes the checks below and would be looked for past the end
    if (m_times.isEmpty() || std::isnan(timeSec))
        {
            return 0.0;
        }
    if (timeSec <= m_times.first())
        {
            return m_values.first();
        }
    if (timeSec >= m_times.last())
        {
            return m_values.last();
        }

    // first sample strictly after timeSec; it has a predecessor because of the checks above
    const int next = int(std::upper_bound(m_times.cbegin(), m_times.cend(), timeSec) - m_times.cbegin());
    const int prev = next - 1;
    if (m_interpolation == Step)
        {
            return m_values.at(prev);
        }
    const double f = (timeSec - m_times.at(prev)) / (m_times.at(next) - m_times.at(prev));
    return m_values.at(prev) + f * (m_values.at(next) - m_values.at(prev));
}
