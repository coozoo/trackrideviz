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
inline constexpr char ElapsedTime[] = "elapsedTime";   // s since the recording began
inline constexpr char Pace[] = "pace";                 // s/km; none while standing
inline constexpr char Power3s[] = "power3secAverage";  // W, the mean of the last 3 s
inline constexpr char Power30s[] = "power30secAverage"; // W, of the last 30 s
// The weather of the day along the ride (weather.h).
inline constexpr char WindSpeed[] = "windSpeed";         // m/s, 10 m above the ground
inline constexpr char WindDirection[] = "windDirection"; // degrees, where it blows from: 0 = north, clockwise
inline constexpr char Headwind[] = "headwind";           // m/s against the rider, at the rider's height; - from behind
inline constexpr char WindRelative[] = "windRelative";   // degrees, where it blows from as the rider sees it: 0 = ahead, 90 = the right
inline constexpr char AirDensity[] = "airDensity";       // kg/m3
inline constexpr char PowerEstimated[] = "powerEstimated"; // W, the estimate beside a recorded power, to compare
inline constexpr char ElevationMax[] = "elevationMax"; // m, the highest point of the recording (the same all along)
inline constexpr char ElevationMin[] = "elevationMin"; // m, the lowest
inline constexpr char DistanceTotal[] = "distanceTotal"; // m, the length of the recording (the same all along)
inline constexpr char Braking[] = "braking";           // g, slowing down as a positive number; 0 otherwise
inline constexpr char FallingSpeed[] = "fallingSpeed"; // m/s downwards; 0 while climbing
inline constexpr char GlideRatio[] = "glideRatio";     // meters forwards per meter down; none unless descending
// The time on the clock is not a channel: gauges bound to these show the recording's own
// clock (TelemetrySession::startUtc) as text.
inline constexpr char TimeOfDay[] = "timeOfDay";
inline constexpr char Date[] = "date";
inline constexpr char DateAndTime[] = "dateAndTime";
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
inline constexpr char BestLapNumber[] = "bestLapNumber";   // which lap the fastest finished so far was
// Within the lap under way, so far (the whole recording when it has no laps)
inline constexpr char SpeedLapMax[] = "speedLapMax";       // m/s
inline constexpr char SpeedLapAvg[] = "speedLapAvg";       // m/s
inline constexpr char PowerLapAvg[] = "powerLapAvg";       // W
// Acceleration in g, recorded or worked out from speed and heading.
inline constexpr char GForceLateral[] = "gForceLateral";           // + to the right
inline constexpr char GForceLongitudinal[] = "gForceLongitudinal"; // + forwards
inline constexpr char GForceVertical[] = "gForceVertical";         // + up
// Pedaling, from a power meter that measures each leg (FIT pedal dynamics).
inline constexpr char PowerBalanceLeft[] = "powerBalanceLeft";   // % of the power from the left leg
inline constexpr char PowerBalanceRight[] = "powerBalanceRight"; // % from the right leg, 100 - the left's
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
