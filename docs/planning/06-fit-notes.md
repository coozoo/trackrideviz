# 6. FIT decoder — notes before writing it

Decision: own decoder (05-decisions §5). This page collects what to get right. The first list is
from the public FIT protocol description and general experience; it is a **checklist to verify
against prior art, not a verified list**. The review tasks at the bottom are still to do.

## Where prior art is (and is not)

| Source | What is there |
|--------|---------------|
| another editor | Nothing to learn: `FITImporter` just launches `FIT2another editorCSV.exe`, a C++/CLI wrapper around Garmin's FIT SDK, and reads the CSV |
| the old editor | FIT/G-Metrix parsing is in native `telemetry.dll` / `Veg.GMetrix.Convert.dll` - not in the decompiled sources. Only the list of metrics it extracts is visible (01-reference-apps §1.1) |
| GoldenCheetah (`src/FileIO/FitRideFile.cpp`, GPL) | The real mine: ~15 years of device-specific workarounds. **Read before coding** |
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

## Review tasks (M1, before the decoder is written)

1. Read GoldenCheetah `FitRideFile.cpp` and note every device-specific workaround here.
2. Collect sample files into `tests/data/fit/` (own recordings only): the old editor with G-Metrix + OBD, Edge, a watch, Wahoo, one truncated, one chained, one with developer fields.
3. Cross-check our decoder's output against `fitdecode` dumps of the same files in the unit tests.
