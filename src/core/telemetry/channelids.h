#pragma once

// Canonical channel ids. Values are stored in the unit given in the comment;
// display units (km/h, mph, feet...) are a presentation setting.
// Names follow the valueType identifiers of .veg gauge files where one exists, so imported
// templates bind without a mapping table.
namespace ChannelId
{
inline constexpr char PositionLat[] = "positionLat";   // degrees
inline constexpr char PositionLon[] = "positionLon";   // degrees
inline constexpr char Elevation[] = "elevation";       // m
inline constexpr char Speed[] = "speed";               // m/s
inline constexpr char Distance[] = "distance";         // m
inline constexpr char Heading[] = "heading";           // degrees, 0 = north, clockwise
inline constexpr char Grade[] = "grade";               // %
inline constexpr char HeartRate[] = "heartRate";       // bpm
inline constexpr char Cadence[] = "cadence";           // rpm
inline constexpr char Power[] = "power";               // W
inline constexpr char Temperature[] = "temperature";   // degrees Celsius
// Acceleration in g. Nothing produces these yet; gauges can already be bound to them.
inline constexpr char GForceLateral[] = "gForceLateral";           // + to the right
inline constexpr char GForceLongitudinal[] = "gForceLongitudinal"; // + forwards
inline constexpr char GForceVertical[] = "gForceVertical";         // + up
}
