# 6. FIT decoder — notes before writing it

Decision: own decoder (05-decisions §5). This page collects what to get right. The first list is
from the public FIT protocol description and general experience; it is a **checklist to verify
against prior art, not a verified list**. The review tasks at the bottom are still to do.

## What the reference apps actually do (checked 2026-11-12)

| Source | Finding |
|--------|---------|
| the old editor | `telemetry.dll` (native) embeds **Garmin's own FIT C++ SDK** - a generic, profile-driven decoder - and registers listeners for: `record`, `lap`, `event`, `device_info`, `gps_metadata`, `accelerometer_data`, `gyroscope_data`, `magnetometer_data`, `barometer_data`, `three_d_sensor_calibration`, `one_d_sensor_calibration`, `timestamp_correlation`, `camera_event`, `obdii_data`, `aviation_attitude`, `nmea_sentence`, and Garmin's private `garmin_iop_data`. There is no handling of other vendors' private fields at all |
| the old editor, GPX side | Reads GPX 1.1 with Garmin extensions: TrackPointExtension v1 (hr, cad, atemp, depth), GpxExtensions v3, **AccelerationExtension v1**, **PressureExtension v1**, PowerExtension v1 and **TrackMovieOffsetExtension v1** (offset of a track inside a movie - the GPX way of syncing) |
| the old editor, derived | Processors for 3D speed, 2D speed from distance, grade, course, trajectory, magnetic calibration/variation, power average, seated/standing time, jump detection (hang time, rotations) from accel + gyro |
| another editor | `FIT2another editorCSV.exe` = an old Garmin FIT SDK (C++/CLI) dumping `record` only: lat, lon, altitude, distance, speed, grade, heart rate, cadence, power, temperature. Its FIT data profile then runs lat/lon through an interpolator with outlier rejection (standard-deviation ceiling 5) - i.e. they met GPS spikes |
| GoldenCheetah `FitRideFile.cpp` | Hand-written decoder, 5.5k lines. Workarounds found: Coros declaring `uint32` with size 1; Bryton repeating timestamps; time going backwards; records arriving while the timer is "stopped" (ignored, "happens all the time"); Zwift lacking `local_timestamp`; truncated bodies kept; chained second file read; unknown messages/fields/base types skipped with a note; smart-recording gaps interpolated only up to a limit. Developer fields are mapped to "XDATA" series |

**Conclusion for vendor-specific data (iGPSPORT, Bryton, Coros, Wahoo...).** None of the three has
special code for third-party devices. What makes a decoder cope with them is being *generic*:
never reject an unknown message or field, expose developer fields by their self-described name,
keep unnamed native fields as raw channels. Ours is built that way (`fit.record.<n>`,
`dev.<name>`). Anything that still looks wrong for a particular device needs that device's file as
a sample - there is no shortcut through the old apps.

## Status of our decoder (2026-11-12)

`src/core/telemetry/fit/`: `FitDecoder` (protocol) + `FitImporter` (record/lap/file_id → session).
Run over the 41 FIT files in GoldenCheetah's test folders (Garmin Edge/Forerunner, Sigma ROX,
Hammerhead Karoo, Suunto, Stryd, Moxy, a watch app export, swim and multisport files):
38 import, 3 are correctly refused as non-activity files, no checksum failures, no crashes.

Found by those files and now handled + unit-tested: one Garmin file has 14 records stamped
**2.16 years ahead** in the middle of the ride. A simple "time must not go backwards" rule then
discards the remaining 95 % of the file. The importer keeps the longest consistent timeline instead
(longest non-decreasing subsequence of timestamps).

Not done yet: the old editor G-Metrix high-rate messages (need a the old editor sample file), names for more native record fields
(29 accumulated_power, 30 left_right_balance, 39 vertical_oscillation, 53 fractional_cadence,
67-72 pedal metrics, 87/88 cycle length, 135+ newer Garmin fields), GPS spike rejection.

## Where prior art is

| Source | What is there |
|--------|---------------|
| GoldenCheetah (`src/FileIO/FitRideFile.cpp`, GPL) | Local copy: `/mnt/nvmestorage/goldencheetah`. Device workarounds and many sample files |
| Garmin FIT SDK docs + `Profile.xlsx` | Message/field numbers, types, scale/offset, units |
| fitdecode / python-fitparse / fit-rs | Clean, readable decoders; good for cross-checking output on sample files |

## Checklist

File structure
- Header is 12 **or** 14 bytes (`header_size` byte says which); the 14-byte one has its own CRC, which may legitimately be 0.
- Files can be **chained**: several FIT files concatenated in one. Loop until EOF.
- Truncated files (camera lost power) are normal: keep everything decoded so far, do not fail the import.
- Data CRC at the end: verify, but treat a mismatch as a warning.

Records
- Definition messages are per *local* message type (0-15) and get **redefined mid-file**; always use the latest.
- Architecture byte per definition: little **or** big endian.
- **Developer fields** (definition header bit 5): extra field block that must at least be skipped correctly; later, decode them (`field_description`, msg 206) - Stryd power, Garmin Varia, CIQ fields live there.
- **Compressed timestamp headers** (header bit 7): 5-bit rolling offset from the last full timestamp.
- Unknown messages and unknown fields must be skipped by size, never treated as an error.
- Field size may be a multiple of the base type size (arrays) or not match it at all (broken writers): trust the size.

Values
- Every base type has its own **invalid value** (0xFF, 0xFFFF, 0x7FFFFFFF…); an invalid field means "no sample", not zero.
- Timestamps: seconds since 1989-12-31T00:00:00Z (Unix + 631065600). Values below 0x10000000 are *relative* (seconds since power-on), not UTC.
- Position: semicircles → degrees = v × 180 / 2³¹.
- Scale and offset come from the profile (altitude: /5 − 500 m; speed: /1000 m/s).
- Prefer `enhanced_speed` / `enhanced_altitude` when present; the 16-bit ones saturate.
- Component fields / accumulated fields (e.g. `compressed_speed_distance`) exist in old files.

Semantics
- `record` (msg 20) carries the 1 Hz samples. Smart recording makes the interval **irregular** - never assume 1 Hz.
- `lap` (19), `session` (18), `event` (21: timer start/stop → pauses), `activity` (34: `local_timestamp` gives the UTC offset).
- Pauses: gaps in records between timer stop/start events. Do not interpolate speed across them.
- `hr` messages (132) carry HR separately for some straps/swim; multi-sport files have several sessions.
- **the old editor G-Metrix** files: high-rate sensor data is in `gyroscope_data` (164), `accelerometer_data` (165), `magnetometer_data` (208), `three_d_sensor_calibration` (167), `gps_metadata` (160, 10 Hz position/speed/heading), `obdii_data` (174), `aviation_attitude` (178), `camera_event` (161 - video start/stop: **this is what syncs data to clips**), `timestamp_correlation` (162 - system ↔ UTC time). Samples are arrays with `sample_time_offset` in ms and need the calibration message applied.

## Real ride: iGPSPORT IGS520 + action camera (checked 2026-11-12)

One ride recorded on the IGS520 and exported three ways (FIT, GPX, TCX), with the camera files.

| Finding | Consequence |
|---------|-------------|
| The FIT is completely standard: `file_id`, `sport`, `session`, `lap`, `record`, `event`, `device_info`, `activity`, `file_creator`. Manufacturer 115, no private messages, no developer fields. Record carries position, altitude, heart rate, cadence, distance, speed, temperature | Nothing vendor-specific to support. Imports with no warnings |
| **The GPX and TCX exports are 3 hours off.** They write the *local* wall-clock time with a `Z` (UTC) suffix: GPX starts `07:47:11Z`, the FIT (correct) `04:47:11Z` | UTC auto-sync cannot trust GPX/TCX from this vendor. Sync must offer whole-hour (and 30/15 min) snapping and show the offset it assumed. Prefer the FIT when both exist |
| The FIT states its UTC offset: `activity.local_timestamp − timestamp` = +3 h | Imported as `TelemetrySession::utcOffsetSeconds()`. Lets us convert local-time sources |
| Smart recording: 1427 records in 5360 s, gaps of 1-15 s. The GPX export keeps only every second one of those (714), the TCX fewer still (476), and neither has heart rate or cadence | Interpolation between records is normal operation, not an edge case. FIT is the only export worth using |
| Auto-pause: 12 timer stop/start pairs, 319 s in total | Imported as `TelemetrySession::pauses()`. Gauges must not interpolate speed across a pause |
| FIT and GPX agree to ~2 m in position and 0.1 m in altitude at the same instants | Independent confirmation that the decoder's scaling is right |
| Distance summed from the sparse GPX points is 0.8 % short of the device's own distance | Keep the device's distance channel when there is one; derive only as a fallback |
| Camera files (`20250715_074932A.mp4`, 1080p60, 18-minute chapters) have **no creation time tag and no telemetry stream**. The start time exists only in the file name, as local time | Video start for auto-sync = file name pattern + the data's UTC offset. Chapters must be joined by name order |

## Samples wanted

Real files are the only test for quirks nobody thought of. They stay out of the repository;
`RTV_FIT_SAMPLES=/dir make check` (or running `tst_fit sampleFiles`) imports every `*.fit` in a
folder and prints what was found. Needed most:

1. the old editor with G-Metrix, ideally with OBD.
2. Anything that another program refused or showed wrongly.
