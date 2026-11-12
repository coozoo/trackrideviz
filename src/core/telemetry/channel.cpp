#include "channel.h"

#include <algorithm>

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

double Channel::valueAt(double timeSec, bool *ok) const
{
    if (ok)
        {
            *ok = !m_times.isEmpty() && timeSec >= m_times.first() && timeSec <= m_times.last();
        }
    if (m_times.isEmpty())
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
