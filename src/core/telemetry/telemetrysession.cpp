#include "telemetrysession.h"

#include <algorithm>

double TelemetrySession::duration() const
{
    double end = 0.0;
    for (const auto &entry : m_channels)
        {
            end = std::max(end, entry.second.endTime());
        }
    return end;
}

const Channel *TelemetrySession::channel(const QString &id) const
{
    const auto it = m_channels.find(id);
    return it == m_channels.end() ? nullptr : &it->second;
}

Channel &TelemetrySession::channelForWriting(const QString &id, const QString &unit, Channel::Interpolation interpolation)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        {
            it = m_channels.emplace(id, Channel(id, unit, interpolation)).first;
            m_order.append(id);
        }
    return it->second;
}

void TelemetrySession::removeChannel(const QString &id)
{
    m_channels.erase(id);
    m_order.removeAll(id);
    m_derived.removeAll(id);
}

void TelemetrySession::removeEmptyChannels()
{
    for (const QString &id : QStringList(m_order))
        {
            const auto it = m_channels.find(id);
            if (it != m_channels.end() && it->second.isEmpty())
                {
                    m_channels.erase(it);
                    m_order.removeAll(id);
                }
        }
}
