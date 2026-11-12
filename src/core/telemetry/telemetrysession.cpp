#include "telemetrysession.h"

#include <algorithm>

double TelemetrySession::duration() const
{
    double end = 0.0;
    for (const Channel &c : m_channels)
        {
            end = std::max(end, c.endTime());
        }
    return end;
}

const Channel *TelemetrySession::channel(const QString &id) const
{
    auto it = m_channels.constFind(id);
    return it == m_channels.constEnd() ? nullptr : &it.value();
}

Channel &TelemetrySession::channelForWriting(const QString &id, const QString &unit, Channel::Interpolation interpolation)
{
    auto it = m_channels.find(id);
    if (it == m_channels.end())
        {
            it = m_channels.insert(id, Channel(id, unit, interpolation));
            m_order.append(id);
        }
    return it.value();
}

void TelemetrySession::removeEmptyChannels()
{
    for (const QString &id : QStringList(m_order))
        {
            if (m_channels.value(id).isEmpty())
                {
                    m_channels.remove(id);
                    m_order.removeAll(id);
                }
        }
}
