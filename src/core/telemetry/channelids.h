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
// Worked out from the others when a recording lacks them (derivedchannels.h)
inline constexpr char GradeAngle[] = "gradeAngle";     // degrees, + uphill
inline constexpr char VerticalSpeed[] = "verticalSpeed"; // m/s, + up
inline constexpr char TotalAscent[] = "totalAscent";   // m since the start
inline constexpr char TotalDescent[] = "totalDescent"; // m since the start
// Laps (lapengine.h): from a start/finish line or the laps the device recorded
inline constexpr char Lap[] = "lap";                       // number, 1 = the first full lap
inline constexpr char LapTime[] = "lapTime";               // s since the lap began
inline constexpr char PreviousLapTime[] = "previousLapTime"; // s, the last finished lap
inline constexpr char BestLapTime[] = "bestLapTime";       // s, the fastest finished so far
inline constexpr char LapDelta[] = "lapDelta";             // s behind (+) the best lap so far at the same point
inline constexpr char LapCount[] = "lapCount";             // finished laps in the recording
inline constexpr char Sector[] = "sector";                 // number within the lap, 1 = from the start/finish line
inline constexpr char SectorTime[] = "sectorTime";         // s since the sector began
inline constexpr char PreviousSectorTime[] = "previousSectorTime"; // s, the last finished sector
inline constexpr char SectorDelta[] = "sectorDelta";       // s, the last finished sector against the best of it so far
// Acceleration in g, recorded or worked out from speed and heading.
inline constexpr char GForceLateral[] = "gForceLateral";           // + to the right
inline constexpr char GForceLongitudinal[] = "gForceLongitudinal"; // + forwards
inline constexpr char GForceVertical[] = "gForceVertical";         // + up
// Pedalling, from a power meter that measures each leg (FIT pedal dynamics).
inline constexpr char PowerBalanceLeft[] = "powerBalanceLeft";   // % of the power from the left leg
inline constexpr char LeftPower[] = "leftPower";                 // W, the power and the balance together
inline constexpr char RightPower[] = "rightPower";               // W
inline constexpr char LeftTorqueEffectiveness[] = "leftTorqueEffectiveness";   // %
inline constexpr char RightTorqueEffectiveness[] = "rightTorqueEffectiveness"; // %
inline constexpr char LeftPedalSmoothness[] = "leftPedalSmoothness";           // %
inline constexpr char RightPedalSmoothness[] = "rightPedalSmoothness";         // %
inline constexpr char CombinedPedalSmoothness[] = "combinedPedalSmoothness";   // %
inline constexpr char LeftPlatformCenterOffset[] = "leftPlatformCenterOffset";   // mm, + outwards
inline constexpr char RightPlatformCenterOffset[] = "rightPlatformCenterOffset"; // mm
// Where on the turn of the crank a leg gives power, and its strongest part:
// a start and a length in degrees, 0 = crank straight up, clockwise seen from the right.
inline constexpr char LeftPowerPhaseStart[] = "leftPowerPhaseStart";
inline constexpr char LeftPowerPhaseLength[] = "leftPowerPhaseLength";
inline constexpr char LeftPowerPhasePeakStart[] = "leftPowerPhasePeakStart";
inline constexpr char LeftPowerPhasePeakLength[] = "leftPowerPhasePeakLength";
inline constexpr char RightPowerPhaseStart[] = "rightPowerPhaseStart";
inline constexpr char RightPowerPhaseLength[] = "rightPowerPhaseLength";
inline constexpr char RightPowerPhasePeakStart[] = "rightPowerPhasePeakStart";
inline constexpr char RightPowerPhasePeakLength[] = "rightPowerPhasePeakLength";
}
