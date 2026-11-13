# 3. Architecture

Build: **qmake**, C++17, flat repo with `main.cpp` + `trackrideviz.pro` in the root - the same
shape as qtjsondiff / qtrfpowermeter, so the release workflow, debian/, .spec and the macOS script
work the same way. Minimum Qt is **6.2.4** (what Ubuntu 22.04 ships; the Linux release job builds
there because linuxdeployqt wants an old glibc). Dev box: Qt 6.11.1, FFmpeg 8.1, GCC 16.

## 3.1 Modules

```
┌────────────────────────── apps ──────────────────────────┐
│  rtv-desktop (Qt Widgets)      rtv-touch (QML, later)    │
│  rtv-cli (headless render, later)                        │
└───────────────┬──────────────────────────┬───────────────┘
                │                          │
┌───────────────▼──────────────────────────▼───────────────┐
│ rtvcore   (src/core;   QtCore + QtGui only — no Widgets, │
│            no Quick, so every front end can link it)     │
│                                                          │
│  telemetry/  Session, Channel, importers, profiles       │
│  derive/     calculators, lap engine, track geometry     │
│  gauge/      gauge model, JSON I/O, foreign importers,   │
│              GaugeRenderer (QPainter)                    │
│  project/    Project, Overlay, Theme, undo commands      │
│  video/      MediaProbe, FrameSource, ExportBackend,     │
│              ExportJob                                   │
└──────────────────────────────────────────────────────────┘
```

Repo layout:

```
trackrideviz/
├── trackrideviz.pro  main.cpp  resources.qrc
├── src/
│   ├── core/core.pri            included by the app .pro and by every test .pro
│   ├── core/{telemetry,derive,gauge,project,video}/
│   └── *.cpp/*.h                widgets (mainwindow, canvas, docks, dialogs)
├── data/{gauges,templates,profiles}/   our own built-in content (later)
├── tests/tests.pro + one dir per test binary
├── translations/                en_US, uk_UA (.qm embedded in the binary)
├── debian/  trackrideviz.spec  MAC_build_RELEASE.sh  .github/workflows/
└── docs/planning/
```

`rtvcore` is not a separate library yet, just `core.pri`: one .pro keeps the packaging scripts
identical to the other apps. It becomes a real static lib when the QML app arrives.

Rule that keeps the QML port cheap: **anything that is not a widget lives in `rtvcore`** and is
exposed as QObjects with properties/signals or as plain value types. Widgets are thin views.

## 3.2 The one render path

As built (`src/core/gauge/gaugerenderer.h`):

```
BoundGauge(gauge, channel, session, units, theme)   // once: colour the images, find units,
                                                    //       labels, scales, scale numbers
BoundGauge::render(QPainter&, target, time)         // per frame: sample and paint
Overlay::render(QPainter&, frame, time)             // all placed gauges of a project
```

A `Gauge` is a description without data or colours. Binding it to a channel of a recording does
everything that is the same for every frame; `render()` is then the same call for:

- preview — painted over the video frame in the canvas widget
- export — painted into a `QImage` (RGBA premultiplied) per output frame, in the worker thread
- library thumbnails
- QML later — `QQuickPaintedItem` (or Canvas Painter) wrapper

A bound gauge is immutable, so one `Overlay` is shared by the preview and a running export.

Speed (measured with `perf` on 1080p frames of imported `.vet` templates: 12-33 ms before, 3-10 ms after):
- each gauge is split into runs of parts that never change after binding (images and fixed
  texts without manipulations) and parts that do; static runs are drawn once per size into a
  picture and copied; graph lines and map routes likewise (per lap), only markers move
- images are cropped to their non-transparent part when bound (needles are full-dial pictures)
- route and graph lines are simplified (Douglas-Peucker, 0.25 design units) and use bevel joins
- clip shapes reach just past the gauge: a pie slice of radius 10 000 cost the rasteriser more
  than everything else together
- gauges are placed on whole pixels, so cached and live parts line up; a painter that scales or
  turns (none of ours does) bypasses the caches

## 3.3 Telemetry model

- `Channel`: id, unit, `QVector<double> t` (seconds from session start) + `QVector<float|double> v`.
  Channels keep their own sample rate (GPS 1–25 Hz, accel 200 Hz, HR 1 Hz) — no forced common grid.
- `TelemetrySession`: UTC start, channels, laps, source info. **Immutable after load** and held by
  `std::shared_ptr<const TelemetrySession>`, so UI thread, preview and export worker all read it
  without locks. Edits (sync, derived channels, laps) produce a new session object.
- `Sampler`: value at time t by binary search + linear interpolation (heading/angles interpolate on
  the circle; lap number and gear are step-held).
- Derived channels are a small DAG of calculators evaluated once after load, not per frame.

## 3.3a Sync (video time ↔ telemetry time)

One number per data source: `offset = telemetryTime − videoTime`. How it is found, in order:

1. **Clock match.** Video start (container creation time, else a date in the file name such as
   `20250715_074932A.mp4`, read as local time) against the session's `startUtc`, using the
   session's UTC offset to convert. Chaptered files are joined first, so there is one video start.
2. **Hour snapping.** Exports that label local time as UTC (seen from iGPSPORT GPX/TCX) and
   cameras with the wrong time zone give errors of whole hours or 30/15 minutes. If the clock match
   lands the video outside the recording but a shifted match lands inside it, propose the shift.
3. **Manual.** Drag the telemetry strip on the timeline, or scrub both and press "lock". Camera
   clocks drift by seconds, so this step is expected even after a good clock match.

The UI always shows which of the three produced the current offset.

## 3.4 Lap engine

Start/finish = a line segment on the map (point + heading + width). Crossing = segment intersection
between consecutive GPS fixes, crossing time interpolated between the fixes (sub-sample accuracy
matters: 10 Hz GPS at 150 km/h is 4 m per fix). Closed and open course, sectors as further lines,
ignore out/in lap, manual lap list override. Outputs channels: `currentLapTime`, `previousLapTime`,
`bestLapTime`, `lapNumber`, `lapDistance`, `deltaToBest` (compare by distance along lap).

## 3.5 Video pipeline

Preview and export have different needs, so they are separate.

### Preview
`QMediaPlayer` → `QVideoSink` → canvas widget draws frame + overlays. Qt 6's FFmpeg backend already
gives hardware decode on Linux/Windows/macOS/Android. The player position drives a `Timeline`
object in core (`positionChanged(ms)`), everything else listens to that.
Risk: frame-exact stepping with QMediaPlayer is approximate. If that hurts the sync UI, swap in our
own libav decoder behind the same `FrameSource` interface.

### Export — three options

| | A. external `ffmpeg` process | B. in-process libav* | C. hybrid (proposed) |
|---|---|---|---|
| How | Render overlay `QImage`s, write raw RGBA to ffmpeg stdin; ffmpeg decodes the source, `overlay` filter, encodes, copies audio | Link libavformat/codec/filter; decode → composite → encode in our process | `ExportBackend` interface; A first, B second |
| Effort | Low — days | High — weeks, plus hw-accel plumbing per platform | A now, B when needed |
| Encoders / hw accel | Whatever the ffmpeg binary has; one command-line flag | Must wire each one | |
| "Runs anywhere, no deps" | Ship a static `ffmpeg` binary next to the app (AppImage/Flatpak/installer) — works on desktop | Fully self-contained | |
| Mobile | No (cannot spawn binaries on iOS; awkward on Android) | **Yes** | B is the mobile path |
| Licence | App stays independent of FFmpeg's licence, even a GPL build with x264 | Linking x264/x265 makes the whole app GPL | |
| Progress / cancel | Parse `-progress pipe:`; kill process | Native | |
| Speed | Pipe of RGBA frames: fine at 1080p, heavy at 4K60 (≈2 GB/s) | Can blend only dirty rects straight into YUV | Mitigate A by sending only the overlay bounding region(s) |

Proposed: **C**. Backend A gets a working exporter early and is the pragmatic desktop answer;
backend B is added before the touch/mobile app and can then become the default.

**As built (M2), backend A** - `src/core/video/`:

```
MediaProbe (ffprobe)  ─► MediaInfo: size, exact fps fraction, duration, bitrate
VideoEncoders::detect ─► encoders a tiny test encode proves usable on this machine
ExportPlan::make      ─► ffmpeg arguments + what to pipe: overlayRect, frameCount, frameTime(n)
ExportJob (own thread)─► QProcess ffmpeg; per frame n: Overlay::render() into an RGBA image of
                          overlayRect only → stdin; signals progress / finished(outcome, message)
```

- ffmpeg reads and decodes the source video itself; nothing but the gauges crosses the pipe.
  Each group of touching gauges is one region (at most 8); the regions are stacked into one
  picture per frame, and ffmpeg `split`s and `crop`s it and overlays each region in its place.
  Gauges in two corners cost two small regions, not the whole 8 MB frame.
- On Linux the child process enlarges its stdin pipe to 1 MB before ffmpeg starts: with 64 KB
  every multi-megabyte picture took dozens of write / wake-up rounds (29 s -> 20 s for 20 s of
  1080p60). Pictures are Qt's ARGB32, given to ffmpeg as `bgra`, so QPainter never converts.
- Each `overlay` filter costs ffmpeg ~2 ms per 1080p frame whatever the region size; hardware
  decoding (`-hwaccel vaapi`) saves CPU but no time here, so it is not used.
- telemetry time of piped frame n = `start + n / fps + syncOffset`, the same formula as the preview.
- The pipe is filled a few frames ahead and refilled on `bytesWritten`, so the worker's event
  loop stays free and `stop()` is handled between frames.
- The overlay filter runs with `eof_action=endall`: the output ends with the last piped picture.
  That makes stopping clean - the job closes the pipe, ffmpeg finishes the file as if the film
  were over. ffmpeg is never killed in the normal course (killing left an unplayable file, and the
  frames still queued were written into a dead pipe: SIGPIPE, a stop in every debugger). Copied
  sound would run on to the end of the source, so on Unix ffmpeg gets SIGINT once the pipe is
  empty, and the file is then rewritten with `-c copy -t <length of the picture>`.
- For the same reason exactly as many pictures are sent as the video has frames (`nb_frames`
  when the container gives it, otherwise the estimate rounded down).
- Quality options differ per encoder family (CRF, QP, CQ, global_quality); `VideoEncoders` hides
  that behind `ExportSettings`. VA-API needs `format=nv12,hwupload` at the end of the filter chain.
- There is no `ExportBackend` interface yet: with one backend it would be an interface with one
  implementation. It is introduced when backend B is written.

## 3.6 Threading (signals/slots, worker-object pattern)

| Thread | Object | Talks via |
|--------|--------|-----------|
| GUI | widgets, `Project`, `Timeline`, `QUndoStack` | direct |
| Import worker | `TelemetryLoader` | `load(path)` slot → `loaded(shared_ptr<const Session>)` / `failed` |
| Derive worker | `DerivePipeline` | `recompute(session, spec)` → `ready(session)` |
| Export worker | `ExportJob` + backend (`QProcess` lives in this thread) | `start/cancel` → `progress/finished/failed` |
| Qt Multimedia internal | decoder | `QVideoSink::videoFrameChanged` (queued) |
| Thumbnail pool | `QtConcurrent` | `QFuture` |

No shared mutable state across threads: workers receive value types or `shared_ptr<const T>` and
answer with new immutable objects over queued signals. Export takes a *snapshot* of the project at
start, so the user can keep editing while it renders.

## 3.7 Desktop UI (Widgets)

`QMainWindow` with docks, layouts saved/restored:

- centre: **Canvas** — video + overlays; select, drag, resize, snap to edges/safe area
- bottom: **Timeline** — scrubber, trim handles, lap markers, a telemetry strip (speed trace) so the
  data is visible against the video; sync offset by dragging the strip
- left: **Sources** (videos, data files, sync state) / **Library** (gauges, templates)
- right: **Properties** (selected overlay: metric, units, range, colours, visibility range)
- tabs/modes: **Map & Laps** (track, start/finish, sectors, lap table), **Data** (table + charts),
  **Gauge editor**, **Export** dialog with presets and queue

Models are `QAbstractItemModel` subclasses in core-adjacent code so QML can reuse them.

## 3.7a Settings

Three places hold choices, and each kind has exactly one home:

| What | Where | Examples |
|------|-------|----------|
| How the *program* behaves on this computer | `AppSettings` (`src/core/settings`, QSettings) → **Settings dialog** | style and colours, language, hardware decoding, display units, later the ffmpeg path and default folders |
| What belongs to one *video project* | project file (`.rtvproj`, M2) | sources, sync offset, gauges and their positions, the units a particular gauge shows |
| How one *export* is made | **Export dialog** (M2); last used values remembered, defaults in `AppSettings` | codec, quality, resolution, hardware encoder, output file |

`AppSettings` is the only code that knows key names and defaults. It is in the core, without
widgets, so the command line and the QML front end read the same file. Values are stored as words
(`dark`, `vaapi`, `mph`), an unknown word falls back to the default.

The dialog follows Overseer's: page list on the left, Save / Cancel, nothing written before Save.
What can change at once does (colours, style, units); language and decoder are read at start-up
and the dialog says so. Pages: **Appearance**, **Preview**, **Units** now; **Export** (ffmpeg
binary, default codec and folder) arrives with M2, because only then is there something to use it.

Units are a display matter only: channels stay SI, `ChannelFormat::displayFor()` (core) gives name,
unit, scale and offset for tables, and built-in gauges are built for the chosen `Units`.

### Gauges and templates

| Kind | Id | Where |
|------|----|-------|
| Drawn in code, always there | `builtin:<name>` | `BuiltinGauges` |
| Ship with the program, read-only | `bundled:<folder>`, `bundled-template:<file>` | `data/` in the sources, installed as below (`BundledData`) |
| The user's own and imported | `library:<folder>`, `template:<file>` | `<application data>/gauges`, `/templates` |

Where the shipped `gauges/` and `templates/` are, first found wins: `$TRACKRIDEVIZ_DATA`;
`<exe>/data` (Windows zip); `<exe>/../share/trackrideviz` (deb/rpm `/usr/share/trackrideviz`,
AppImage, any `make install` prefix); `<exe>/../Resources` (macOS bundle, `QMAKE_BUNDLE_DATA`);
the system data folders; in debug builds the source tree's `data/`. Shipped artwork is drawn by
`tools/makebundled` (pictures) and `tools/vectorgauges/make.py` (SVG gauges) - run them, look at
the result, commit `data/` - until the gauge editor exists; only our own artwork goes there.
`tools/gaugereview` renders a gauge from elsewhere and ours side by side on a local page; its
output shows other people's artwork and stays out of the repository.

SVG drawings in gauges are drawn as vectors at the size they end up. They name the theme's
colours as `@foreground`, `@background`, `@accent`, `@accent2`, and their transparency as
`@<name>-opacity` (Qt SVG reads no transparency inside a colour). Texts use the bundled
Barlow Semi Condensed (`GaugeFonts`).

## 3.8 Touch UI (QML, later)

Not a port of the desktop app: a 4-step flow — pick video → confirm sync → pick template → export.
Reuses `rtvcore` whole; needs backend B and a `QQuickPaintedItem` gauge wrapper. No gauge editor.

## 3.9 From Overseer

`gpxtrack.{h,cpp}` and `gpxplayer.{h,cpp}` are the starting point for the GPX importer and the
`Sampler` (time→position interpolation, heading). They need widening: all segments/tracks, extensions
(hr/cad/power/temp), speed. The replay window's play/seek/speed controls are a UI reference.
