# 4. Roadmap

Each milestone ends with something usable; nothing is built "for later" before it is needed.

## M0 — Repo skeleton ✅ (2026-11-12)
Repo, README, GPL-3.0, qmake project, main window shell with docks, en/uk translations,
`Channel` + first unit test, debian/spec/macOS/Windows packaging and both workflows.
Verified: local build + tests (Qt 6.11), deb build + tests in an Ubuntu 22.04 container (Qt 6.2.4).
Not yet verified (needs a push to GitHub): AppImage, macOS and Windows jobs.

## M1 — First overlay on screen (vertical slice)
- GPX + FIT import → `TelemetrySession` (FIT: see [06-fit-notes.md](06-fit-notes.md) first)
- Open a video, play/seek (`QMediaPlayer` + `QVideoSink` canvas)
- Sync by UTC with manual offset
- Gauge model + `GaugeRenderer` with `image`, `text`, `dynamicRotate`
- One hard-coded speed gauge drawn over the video
- Tests: importers, sampler

**Proves:** telemetry model, render path, preview.

## M2 — First exported video
- `ExportBackend` A (ffmpeg process + RGBA pipe), `ExportJob`, progress/cancel, audio copy
- Export dialog: codec, bitrate/CRF, resolution, hw encoder detection
- Project save/load (`.rtvproj`)

**Proves:** the whole chain end to end. From here the app is already useful.

## M3 — Real gauges
- Remaining the old editor components: clips, translate, group, graphLine, trackLine, animationStrip
- Theme colours, metric binding, units
- the old editor `.veg` / `.vet` import → library; apply template
- Canvas editing: select/move/scale, edge anchors, undo/redo
- Our own small built-in gauge set (original artwork) so the app works without the old editor assets

## M4 — Track day features
- Lap engine: start/finish on map, sectors, lap table, best/previous/delta channels
- Map & Laps view, lap markers on the timeline
- Derived channels: G from GPS, distance, grade, smoothing, math
- Data viewer + charts; timeline telemetry strip and drag-to-sync

## M5 — More data
- GoPro GPMF embedded telemetry, chapter auto-join
- CSV with data profiles; another editor profile import; TCX, NMEA, VBO
- Multiple data sources per project

## M6 — Gauge editor
- Built-in editor (components tree, properties, live preview with sample data)
- Vector primitives, non-linear scales; another editor gauge import
- G-plot, ticker tape, lap table, sector bar

## M7 — Polish and distribution
- Export backend B (in-process libav) if/when needed; overlay-only alpha export; export queue
- AppImage + Flatpak, Windows installer, macOS bundle
- Translations (uk, en), user manual

## M8 — Touch UI
- QML app on `rtvcore` for tablets/phones (needs backend B)

## Later / maybe
Multi-camera picture-in-picture, side-by-side lap comparison with ghost, track database with
auto-detected start/finish, 360 video, `rtv-cli` batch rendering, live OBD.
