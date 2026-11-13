#include "dataedits.h"

#include "channelids.h"

namespace
{
// the channels an edit of `id` takes samples out of
QStringList partners(const QString &id)
{
    if (id == ChannelId::PositionLat || id == ChannelId::PositionLon)
        {
            return {ChannelId::PositionLat, ChannelId::PositionLon};
        }
    return {id};
}
}

TelemetrySessionPtr DataEdits::apply(const TelemetrySessionPtr &recorded, const QVector<DataEdit> &edits)
{
    if (!recorded || edits.isEmpty())
        {
            return recorded;
        }
    auto session = std::make_shared<TelemetrySession>(*recorded);
    for (const DataEdit &edit : edits)
        {
            for (const QString &id : partners(edit.channel))
                {
                    if (session->hasChannel(id))
                        {
                            Channel &channel = session->channelForWriting(id, recorded->channel(id)->unit(),
                                                                          recorded->channel(id)->interpolation());
                            channel.removeBetween(edit.start, edit.end);
                        }
                }
        }
    return session;
}

QVector<DataEdit> DataEdits::restored(const QVector<DataEdit> &edits, const QString &channel, double start, double end)
{
    QVector<DataEdit> left;
    for (const DataEdit &edit : edits)
        {
            const bool same = partners(edit.channel).contains(channel);
            if (!same || edit.end < start || edit.start > end)
                {
                    left.append(edit);
                }
        }
    return left;
}
