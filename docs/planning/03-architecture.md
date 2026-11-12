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

```
GaugeRenderer::paint(QPainter&, const Gauge&, const Theme&, const FrameContext&)
```
`FrameContext` = telemetry time + a sampler giving channel values (and history windows for graphs,
track polyline for maps). The same call is used by:

- preview — painted over the video frame in the canvas widget
- export — painted into a `QImage` (ARGB32_Premultiplied) per output frame
- gauge editor, library thumbnails, template previews
- QML later — `QQuickPaintedItem` (or Canvas Painter) wrapper

Static layers (dial faces, tick marks) are cached per gauge as pixmaps; only the dynamic components
repaint. A gauge whose inputs did not change between two frames is not repainted at all.

## 3.3 Telemetry model

- `Channel`: id, unit, `QVector<double> t` (seconds from session start) + `QVector<float|double> v`.
  Channels keep their own sample rate (GPS 1–25 Hz, accel 200 Hz, HR 1 Hz) — no forced common grid.
- `TelemetrySession`: UTC start, channels, laps, source info. **Immutable after load** and held by
  `std::shared_ptr<const TelemetrySession>`, so UI thread, preview and export worker all read it
  without locks. Edits (sync, derived channels, laps) produce a new session object.
- `Sampler`: value at time t by binary search + linear interpolation (heading/angles interpolate on
  the circle; lap number and gear are step-held).
- Derived channels are a small DAG of calculators evaluated once after load, not per frame.

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

`ExportJob` (QObject in a worker thread): for output frame n → t = n / fps → telemetry time =
t + syncOffset → `GaugeRenderer` → backend. Signals `progress(frame, total, fps)`, `finished`,
`failed(msg)`; slot `cancel()`.

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

## 3.8 Touch UI (QML, later)

Not a port of the desktop app: a 4-step flow — pick video → confirm sync → pick template → export.
Reuses `rtvcore` whole; needs backend B and a `QQuickPaintedItem` gauge wrapper. No gauge editor.

## 3.9 From Overseer

`gpxtrack.{h,cpp}` and `gpxplayer.{h,cpp}` are the starting point for the GPX importer and the
`Sampler` (time→position interpolation, heading). They need widening: all segments/tracks, extensions
(hr/cad/power/temp), speed. The replay window's play/seek/speed controls are a UI reference.
