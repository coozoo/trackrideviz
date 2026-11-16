#include "datasources.h"

#include "telemetryimporter.h"
#include "../units/units.h"

#include <QFileInfo>

bool DataSources::clockShift(const TelemetrySession &main, const TelemetrySession &added, double *shift)
{
    if (!main.startUtc().isValid() || !added.startUtc().isValid())
        {
            return false;
        }
    // time t of the added file is its start + t, which is main start + shift + t
    *shift = main.startUtc().msecsTo(added.startUtc()) / 1000.0;
    return true;
}

QStringList DataSources::missingChannels(const TelemetrySession &main, const TelemetrySession &added)
{
    QStringList missing;
    for (const QString &id : added.channelIds())
        {
            if (!main.hasChannel(id))
                {
                    missing << id;
                }
        }
    return missing;
}

TelemetrySessionPtr DataSources::merged(const TelemetrySessionPtr &main, const QVector<DataSource> &sources,
                                        const QVector<TelemetrySessionPtr> &added)
{
    if (!main || sources.isEmpty())
        {
            return main;
        }
    auto session = std::make_shared<TelemetrySession>(*main);
    QStringList names;
    for (int i = 0; i < sources.size(); ++i)
        {
            const DataSource &source = sources.at(i);
            const TelemetrySessionPtr from = added.value(i);
            const QString name = QFileInfo(source.path).fileName();
            if (!from)
                {
                    session->addWarning(tr("%1 could not be read; its channels are left out.").arg(name));
                    continue;
                }
            names << name;
            for (const QString &id : source.channels)
                {
                    const Channel *channel = from->channel(id);
                    if (!channel)
                        {
                            continue;
                        }
                    // the added file's in place of the main data's
                    session->removeChannel(id);
                    Channel &into = session->channelForWriting(id, channel->unit(), channel->interpolation());
                    for (int s = 0; s < channel->count(); ++s)
                        {
                            const double time = channel->timeAt(s) + source.shift;
                            if (time >= 0.0)
                                {
                                    into.append(time, channel->valueAtIndex(s));
                                }
                        }
                    if (into.isEmpty())
                        {
                            session->removeChannel(id);
                            session->addWarning(tr("%1 of %2 lies wholly before the main data; check its time shift.")
                                                .arg(ChannelFormat::displayFor(*channel, Units()).name, name));
                        }
                }
        }
    if (!names.isEmpty())
        {
            session->setSourceInfo(tr("%1, with %2").arg(main->sourceInfo(), names.join(", ")));
        }
    return session;
}

TelemetrySessionPtr DataSources::loadAndMerge(const TelemetrySessionPtr &main, QVector<DataSource> *sources)
{
    if (!main || sources->isEmpty())
        {
            return main;
        }
    QVector<TelemetrySessionPtr> added;
    for (DataSource &source : *sources)
        {
            const TelemetrySessionPtr session = TelemetryImporter::loadRecorded(source.path);
            double shift = 0.0;
            if (session && !source.shiftIsManual && clockShift(*main, *session, &shift))
                {
                    source.shift = shift;
                }
            added.append(session);
        }
    return merged(main, *sources, added);
}
