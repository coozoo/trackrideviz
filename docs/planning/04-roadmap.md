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

## M1.5 — Settings ✅ (2026-11-12)
- `AppSettings` in the core; settings dialog with Appearance (style, light/dark, language, window
  layout), Preview (hardware decoding, preferred decoder) and Units (speed, length, temperature)
- Channel table and the built-in speedometer follow the units
- Where each kind of setting lives: [03-architecture §3.7a](03-architecture.md)

## M2 — First exported video ✅ (2026-11-12)
- ✅ Export through an external `ffmpeg` (`ExportPlan` + `ExportJob`, 03-architecture §3.5): ffmpeg
  decodes the source itself, the application pipes only the gauge area as RGBA, ffmpeg overlays,
  encodes and passes the sound through. Progress; **stopping keeps a playable video** of the part
  done so far; a failed export leaves no file
- ✅ Export dialog: encoder (only those a test encode proves usable here), bitrate or constant
  quality, speed, size, sound, range; last choices remembered
- ✅ Settings page *Export*: ffmpeg program, folder for exported videos
- ✅ Project save/load (`.rtvproj`, JSON): video, data, sync, gauge placement
- ✅ `Overlay` (gauge placement in fractions of the frame) shared by preview and export

Checked on the real ride (1080p60, Intel UHD 620): 20 s exported in 10.5 s with `h264_vaapi`,
108 s with `libx264` medium; gauge and sync identical to the preview; sound copied.
Constant quality turned out to be the wrong default for this kind of footage - CRF 20 gave
64 Mbit/s and VA-API QP 20 gave 154 Mbit/s from a 26.6 Mbit/s source - so the dialog starts in
bitrate mode at the bitrate of the source and shows the expected file size.

Not done: no warning about unsaved project changes on closing; one VA-API device assumed
(`/dev/dri/renderD128`); NVIDIA, AMD, Quick Sync and VideoToolbox options are written from the
ffmpeg documentation and only offered when the test encode passes, but never ran on such hardware.

**Proves:** the whole chain end to end. From here the app is already useful.

## M3 — Real gauges

### M3a — gauge engine, library, editing ✅ (2026-11-12)
- ✅ Gauge model as a superset of `.veg` gauges: image, text, group; manipulations rotate, radial clip,
  translate, width clip, height clip (with own value range, wrap, reverse, clip rectangle)
- ✅ A gauge is bound to a channel when placed; scale, unit and label come from that channel,
  round scale numbers from the data (`MetricRange`), colours from the project's theme
- ✅ Native format: a folder with `gauge.json` + images (`GaugeFile`)
- ✅ `.veg` import into the user's library (`VegGaugeImporter`, `GaugeLibrary`)
- ✅ Library dock with pictures; add to video; select, move, resize, delete on the canvas;
  what a gauge shows and the theme colours in the Properties dock; undo/redo; unsaved-changes prompt
- ✅ Built-in gauges with our own artwork: dial, readout
- Project file version 2 (channel per gauge, theme); version 1 still opens

Checked against a collection of 255 `.veg` gauges: 229 import (194 complete, 35 with
parts left out), 26 are refused - 18 are nothing but a logo, 8 are only a track map or lap
circuit. A contact sheet of the imported dials, fills, tapes and combination gauges, bound to the
real ride, was compared by eye: needles, arc fills, numbering and tapes sit where they should.
Not compared pixel for pixel against the program the files come from - it does not run here.

### M3b — every `.veg` part, templates, speed ✅ (2026-11-12)
- ✅ Graph (the channel over the whole recording, with markers at the current point), track map,
  lap circuit (route of the current lap with its start line), sprite animations driven by the
  value, two-axis movement (g-force ball), fixed radial masks, slanted bar ends, channel icons
  (drawn in code)
- ✅ `.vet` templates: imported into a template library, gauges they need imported along;
  "Apply to video" places them for the video's own shape (1920 x 1080 reference, edge anchors)
- ✅ Library dock: Gauges / Templates; one Import for a folder of `.veg` / `.vet` files
- ✅ Drawing 2-5x faster: static runs and route / graph lines drawn once per size and copied,
  images cropped to their content, lines simplified, clip shapes sized to the gauge
- ✅ Export: each group of gauges is piped as its own region (stacked in one picture, cut apart
  by ffmpeg) instead of the whole frame; ffmpeg's input pipe enlarged to 1 MB

Checked against the same collection: 237 of 255 gauges import (2 miss an image the collection itself
lacks; 17 logos and a target mark are refused on purpose), 89 of 90 templates (the other uses
only a gauge that is not there). Contact sheets of all newly supported parts and 24 templates,
drawn on frames of the real ride, compared by eye. 20 s of 1080p60 with a five-gauge imported
template exported in 20-30 s with VA-API on the UHD 620 laptop (was 46 s before the region split
and the pipe size); ffmpeg's four overlay filters alone take 19 s of that.

### Shipped gauges and templates ✅ (2026-11-13)
- ✅ `data/gauges`, `data/templates` packed with every package (deb/rpm, AppImage, Windows zip, macOS
  bundle) and found next to the program; read-only in the library
- ✅ Own set: Arc, Bar, Column, Profile, Route map, Lap map, Icon readout, G-force; templates Ride,
  Track day, Climb, Minimal

### XML gauges ✅ (2026-11-13)
- ✅ Gauge model: `Shape` (rectangle, ellipse, arc, pie, line; pen or fill, gradient), `Dial`,
  `Tape`, non-linear value curves, a fixed unit per part, text formats (whole number, running time,
  degrees-minutes-seconds, time of day), texts in a box, graphs over a moving window
- ✅ `XmlGaugeImporter`; one Import for `.veg` / `.vet` files and XML gauge folders
- ✅ Checked on a full installation: 185 of 185 import, 28 with a note (chart labels, G-plot
  stick and tail, digit pictures); contact sheets of all of them compared by eye
- ✅ XML templates (`project.cdp`) with the gauges they carry, version 2 gauges too: 20 of 20
  of a full installation, laid out as in the originals
- Data profiles are not imported yet (M5)

### M3c — what is left
- ✅ G-force comes from GPS now (M4); without data a part rests at 0, not at its scale's start
- Fonts named by gauges may be missing (Furore, Roboto Cn); the system substitutes
- A "nav" knob (arrow) is not turned to the direction of travel; the format's intent is unclear
- Templates are placed for the video open when applied; a later video of another shape keeps
  the fractions, not the edge anchors
- Saving the current layout as a template; units a single gauge shows

## M4 — Track day features
- ✅ Derived channels (2026-11-13, `DerivedChannels`), for every source, only where the recording
  lacks them: distance from positions (glitches skipped), speed from distance, heading between
  positions a moment apart (held while standing), grade over ±20 m of distance from smoothed
  elevation, grade angle, total ascent/descent (2 m hysteresis), vertical speed, G forwards from the
  change of speed and sideways from speed × turn rate. Even grid of 0.1-1 s, nothing inside pauses.
  Checked on the IGS520 ride: 3 % read on a climb of 3 %, ascent = descent = 249 m on a round trip.
  Shown in italics in the channel list
- Lap engine: start/finish on map, sectors, lap table, best/previous/delta channels
- Map & Laps view, lap markers on the timeline
- Data viewer + charts; timeline telemetry strip and drag-to-sync

## M5 — More data
- GoPro GPMF embedded telemetry, chapter auto-join
- ✅ CSV with data profiles; XML data profile import (2026-11-13)
- AiM and DriftBox CSV layouts; our own profiles for common loggers; TCX, NMEA, VBO
- Multiple data sources per project

## M6 — Gauge editor
- Built-in editor (components tree, properties, live preview with sample data)
- Path shapes; G-plot with a tail, lap table, sector bar

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
