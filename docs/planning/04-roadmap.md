# 4. Roadmap

Each milestone ends with something usable; nothing is built "for later" before it is needed.

## M0 — Repo skeleton ✅ (2026-11-12)
Repo, README, GPL-3.0, qmake project, main window shell with docks, en/uk translations,
`Channel` + first unit test, debian/spec/macOS/Windows packaging and both workflows.
Verified: local build + tests (Qt 6.11), deb build + tests in an Ubuntu 22.04 container (Qt 6.2.4).
Not yet verified (needs a push to GitHub): AppImage, macOS and Windows jobs.

## M1 — First overlay on screen (vertical slice) ✅ (2026-11-12)
- ✅ GPX + FIT import → `TelemetrySession` (see [06-fit-notes.md](06-fit-notes.md)); tested on 41 real FIT files
- ✅ Open a video, play/seek (`QMediaPlayer` + `QVideoSink` → `VideoCanvas`, OpenGL)
- ✅ Sync: clock match from the camera file name + the data's UTC offset, whole-hour snapping when
  the zones disagree, manual offset (`VideoSync`, 03-architecture §3.3a)
- ✅ Gauge model + `GaugeRenderer` with `image`, `text`, `dynamicRotate`
- ✅ One built-in speedometer (artwork drawn in code) over the video, fixed position
- ✅ Tests: importers, sampler, sync, renderer (75 cases); also built and run on Qt 6.2.4

Checked on the real ride (IGS520 FIT + 1080p60 camera chapters): offset found automatically from
the FIT; the mislabelled GPX export of the same ride lands on the same instant through hour
snapping; the channel table shows the value of every channel at the frame on screen.

Preview cost, 1080p60 on a laptop with Intel UHD 620 (measured, whole process):
first version (`toImage()` + QPainter scaling on the GUI thread) 215 % CPU and visibly slow;
now (`QOpenGLWidget`, YUV planes uploaded as textures, shader converts and scales) 108 % with
software decoding, 46 % with VA-API. On Linux the application asks Qt Multimedia for VA-API first,
because Qt's default order stops at CUDA on machines that only have the NVIDIA libraries installed
and then decodes in software. What is left is mostly copying decoded frames back from the GPU;
avoiding that needs Qt's own video output (a QML `VideoOutput`), an option for the M8 front end.

Left for later milestones: speed is still interpolated across auto-pauses; 10-bit video takes the
slow `toImage()` path; chapters are not joined yet.

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
auto-detected start/finish, 360 video, live OBD.

**Command line (`trackrideviz --render project.rtvproj -o out.mp4`, and video + data + template
without a project).** Cheap once M2 exists, because project loading and `ExportJob` live in the
core and need no window: it is the same binary started with `QT_QPA_PLATFORM=offscreen` logic or a
`QGuiApplication`. Plan it right after M3 (templates), when there is something worth batch-rendering.
