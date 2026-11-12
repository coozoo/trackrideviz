#pragma once

#include <QImage>
#include <QString>

// Small pictures of what a channel measures: a heart for heart rate, a
// mountain for elevation... White on transparent, square; gauges colour them
// like any other artwork. Drawn in code, so they belong to this program.
namespace ChannelIcons
{
QImage icon(const QString &channelId, int size = 128);
}
