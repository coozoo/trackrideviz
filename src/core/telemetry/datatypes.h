#pragma once

#include <QString>

// The data types other programs name measurements by ("Speed (MPH)",
// "Altitude / Z Position (Feet)"), as XML gauges and data profiles use them,
// and the units those come in.
namespace DataTypes
{
// A data type -> our channel id ("speed") and the unit it is measured in
// ("mph"; empty when it has none). Types we have no channel for get an id
// made from their name ("Engine Oil Temperature" -> "engineOilTemperature").
QString channelFor(const QString &dataType, QString *unit = nullptr);

// How a value in `unit` becomes what channels store (channelids.h): SI for
// speeds and lengths, degrees Celsius. stored = value * scale + offset.
// False, and the value kept as it is, for units that are not converted.
bool toStored(const QString &unit, double *scale, double *offset, QString *storedUnit);

// The unit a channel of ours is stored in ("deg", "g", "bpm"...); empty for others.
QString storedUnitOf(const QString &channelId);
}
