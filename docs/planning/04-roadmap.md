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
the FIT; the mislabeled GPX export of the same ride lands on the same instant through hour
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
  round scale numbers from the data (`MetricRange`), colors from the project's theme
- ✅ Native format: a folder with `gauge.json` + images (`GaugeFile`)
- ✅ `.veg` import into the user's library (`VegGaugeImporter`, `GaugeLibrary`)
- ✅ Library dock with pictures; add to video; select, move, resize, delete on the canvas;
  what a gauge shows and the theme colors in the Properties dock; undo/redo; unsaved-changes prompt
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
- ✅ Saving the gauges on the video as a template (2026-11-14, File menu): each measured from
  the edge it is nearest to, with a name and a category
- ✅ Units a single gauge shows (2026-11-15): "Units" in a gauge's menu on the video - as in the settings,
  metric or imperial; kept in the project and in templates

## M4 — Track day features
- ✅ Derived channels (2026-11-13, `DerivedChannels`), for every source, only where the recording
  lacks them: distance from positions (glitches skipped), speed from distance, heading between
  positions a moment apart (held while standing), grade over ±20 m of distance from smoothed
  elevation, grade angle, total ascent/descent (2 m hysteresis), vertical speed, G forwards from the
  change of speed and sideways from speed × turn rate. Even grid of 0.1-1 s, nothing inside pauses.
  Checked on the IGS520 ride: 3 % read on a climb of 3 %, ascent = descent = 249 m on a round trip.
  Shown in italics in the channel list
- ✅ Lap engine (2026-11-13, `LapEngine`): a start/finish line placed by clicking the route (40 m
  across the direction of travel there, kept in the project); laps between crossings in the first
  crossing's direction, timed between samples, none within 20 s of the last (jitter at the line).
  Without a line the device's laps are used. Channels: lap, lap time, previous, best so far, lap
  count, delta to the best lap so far at the same point of the lap (by distance)
- ✅ Laps panel: route map with the line and the current position, lap table (best in bold,
  double-click goes to the lap)
- ✅ Sectors (2026-11-13): sector lines placed on the route like the start/finish line (kept in
  the project as `sectors`), each crossed once a lap, either way. A lap that crosses them all gets
  its splits; the lap table shows sector times, the fastest of each in purple. Channels: sector,
  sector time, previous sector, that against the best of it so far
- ✅ Timeline (2026-11-13): the video and the data where the offset puts them, a trace of a chosen
  channel, laps as bands with sector ticks (the best outlined), pauses hatched and the playhead.
  Click or drag to go there, wheel to zoom about the mouse (Shift: pan), Ctrl+drag slides the data
  against the video (sets the offset)
- ✅ Laps compared side by side (2026-11-13): laps selected in the lap table are drawn against
  distance into the lap (a chosen channel), with the time each is behind the fastest of them at the
  same point below; hovering reads all of them at one place
- ✅ Data viewer and corrections (2026-11-13, `DataEdits`): a Data tab with ticked channels as
  charts on the data's time axis (min/max per pixel, so a single spike shows), hover values,
  zoom, click to go there, and a table of one channel's samples. Samples picked by Shift+drag or
  in the table are taken out (latitude and longitude together) and put back; kept in the
  project and undoable. The order is: recording, corrections, derived channels, laps - so speed,
  distance, grade and laps follow a removed GPS jump
- ✅ Cuts (2026-11-13, `CutList`): parts of the video left out of the export, marked A–B on the
  timeline (`[` `]`, Shift+drag, Delete cuts) or found in the data ("Cut stops": speed under
  0.5 m/s or a device pause for 10 s or more, 2 s kept at each end). Kept in the project, undoable,
  skipped while playing. The export drops them with ffmpeg's select, the sound with atrim + concat (it is then AAC);
  gauges keep the real values of the moments that are kept (lap times stay true)
- ✅ Join (2026-11-13, `VideoJoin`, `SyncMap`): File > Join videos copies the streams of several
  videos of one kind into one file (ffmpeg concat, no re-encoding), camera chapters put in order
  (GX01xxxx, GX02xxxx...). Breaks between recordings are found from their times (file name, else
  creation_time) and kept in the project as shifts: telemetry time = video time + offset + the
  part's shift, so the data stays in step after each break and one offset still tunes it all.
  Checked on four camera files: three chapters followed on, a 36:37 break before the fourth
- Joining videos of different kinds (re-encoding), keeping camera data streams (GPMF) in the join

## Maps
- ✅ Street maps under route and lap gauges (2026-11-14, `TileStore`): a map part of a gauge may ask
  for one (`"tiles": true`, its strength and rounded corners). Tiles come from a tile server
  (OpenStreetMap's by default; another address and its credit line in Settings > Maps, where
  downloading can also be switched off), are kept on disk, and the credit is drawn in the map's
  corner. The tiles depend only on the area, not on the size drawn: a ride previewed once exports
  without a connection. Routes are drawn in the Mercator projection now, as the maps are. Gauges
  Route map streets and Lap map streets
- ✅ A choice of maps (2026-11-14): OpenStreetMap, CyclOSM, OpenTopoMap, satellite pictures,
  OpenCycleMap (with the user's key) or the user's own server, one for the whole program, in
  Settings > Maps. Any gauge with a route gets a street map, or loses the one it was drawn with,
  from its menu on the video ("Street map"); the choice is kept in the project and in templates
  (`"streets"`). The credit on the map can be switched off for videos that name the map's makers
  elsewhere. Packages: the Windows one now carries libssl / libcrypto and the AppImage Qt's `tls`
  plugins - to be confirmed on the first packages built with them
- A map of its own per gauge
- ✅ The Laps panel draws its route on the street map (2026-11-14), with a switch and the choice
  of map under it - the same choice as in the settings, so the gauges follow it
- ✅ The route map of the Laps panel zooms (wheel, + and -) and moves (drag); what is picked in the
  Data viewer is drawn on it in red (2026-11-15; no second map in the Data viewer)
- ✅ Elevation from a terrain model (2026-11-14, `Terrain`): the height of the ground under the
  route from "terrarium" height tiles (about 25 m to a pixel), fetched and kept like map tiles by
  a second `TileStore`. Per project, in the Sources panel: for recordings without an elevation
  (the default), in place of the recorded one, or never. Grade, climb and vertical speed are
  worked out from it. A road on a bridge, in a cutting or a tunnel gets the ground's height
- ✅ A map that shows only the part of the route around the rider, enlarged (2026-11-14): a map
  part with `"span"` (meters across) keeps the rider in the middle and moves the map under them,
  north up. With a street map the tiles of the whole way are fetched when the gauge is placed.
  Gauges Route map close-up and Route map streets close-up; any gauge with a route is switched
  between the whole route and 300 m to 5 km around the rider from its menu on the video
  ("Close-up", kept in projects and templates as `"closeUp"`)
- ✅ The library panel finds gauges and templates by name and by group (2026-11-14): where they
  come from, what a gauge shows (maps, speed, laps, ...), a template's category
- ✅ The close-up turned so that the way ahead points up (`"headingUp"`, in the same menu); a
  question before an export while map tiles or terrain heights are still being fetched; a button
  in Settings > Maps that deletes the stored tiles and says how much they take (2026-11-14)

## M5 — More data
- ✅ GoPro embedded telemetry (2026-11-15, `GpmfImporter`): "Open data" on the camera's own .mp4
  reads its positions, elevation and speed (GPS5 and GPS9), on the video's clock, so nothing is
  to be synchronized. Checked with made-up files only: no camera file was at hand
- GoPro: chapters joined by themselves; accelerometer and gyroscope
- ✅ CSV with data profiles; XML data profile import (2026-11-13)
- AiM and DriftBox CSV layouts; our own profiles for common loggers; NMEA, VBO
- ✅ TCX (2026-11-15, `TcxImporter`): track points of every lap with heart rate, cadence, speed and
  power; the file's laps when it has more than one
- Multiple data sources per project
- Power estimated for rides without a power meter (there is none now), shown as an estimate,
  never in place of a recorded power. In three steps:
  1. ✅ (2026-11-15, `PowerEstimate`, the Power page of the settings) The estimate itself: air drag, rolling resistance, gravity and acceleration from speed and
     the smoothed elevation, with a drivetrain loss; no power while braking or with the cranks
     still. A settings page with the rider's weight and height, the bike's weight, the tires
     (road, gravel, mountain bike, each fast, typical or slow, or a rolling resistance of the
     user's own; a tire size such as 700x25 or 26x1.95 only picks the kind), the riding position
     (tops, hoods, drops, aero bars, mountain bike upright, or a drag area of the user's own) and
     the switch. Beside a recorded power the estimate is a measurement of its own, to compare
     the two
  2. ✅ (2026-11-15, `WeatherStore`: Open-Meteo, Visual Crossing and WeatherAPI.com, the last two
     with the user's key and not tried against the real services; a wind by hand in the Sources dock)
     Wind and air: the wind's speed and direction, the temperature and the pressure of the day
     of the ride from a weather history service, asked for at points along the route and hour by
     hour, so a ride between towns gets each place's weather; off until switched on, the wind
     brought down from the 10 m it is measured at to the rider's height; a wind entered by hand
     when there is no network. A point every 10 km by default, to be changed by the user (the
     services' own grid is 10-25 km and an hour, so finer brings little). Services to choose
     from: Open-Meteo (no key, free for non-commercial use), Visual Crossing and WeatherAPI.com
     (each with a key and a free allowance)
  3. ✅ (2026-11-15: "Mark surface" in the Laps panel, the Surfaces page of the settings) Surfaces: stretches of the ride marked as smooth, typical or rough asphalt, gravel, dirt,
     chosen as the lap sections are and kept in the project; typical asphalt where nothing is
     marked. The rolling resistance is the tire's times the surface's
- ✅ Measurements imported gauges ask for, worked out from the ride (2026-11-14): running time,
  pace, power averaged over 3 and 30 seconds, the highest and lowest point, the whole distance,
  braking, falling speed, glide ratio (`DerivedChannels`); the lap's top and average speed and
  average power so far, the number of the best lap (`LapEngine`; the whole recording when it has
  no laps). Gauges bound to the time of day or the date show the recording's clock. Other names
  for these in imported gauges are mapped (`DataTypes::canonical`). Engine, boat, dive and flight
  measurements need a logger's file and come with its profile

## M6 — Gauge editor
- ✅ SVG drawings in gauges (2026-11-13): sharp at any size, theme colors inside the drawing;
  Barlow Semi Condensed (OFL) bundled for gauge texts
- ✅ `.rtvgauge` / `.rtvtemplate` (2026-11-13): one gzipped JSON file to pass a gauge or a template
  with its gauges on; Export and Import in the library panel. Built-in gauges and templates marked
  with a padlock. Next: file associations (MIME types) so a double click imports
- ✅ Project font (2026-11-13): font, weight and "also gauges with a font of their own" next to the
  theme colors, kept in projects and templates; Barlow Semi Condensed bundled in all weights
- Per gauge on the video: own font, size, colors, opacity (later)
- ✅ Charts (2026-11-14): one smoothing for Data, Timeline and Compare laps (none, 2, 5, 15 s, display
  only); the Data chart draws each pixel's mean with its span as a faint band
- ✅ Own vector set, batch 1 (2026-11-14, `tools/vectorgauges`, reviewed one by one against what
  they replace with `tools/gaugereview`): Dial, Dial metal, Dial metal arc, Dial ring, Dial sport,
  Dial glass, Dial small, Dial small metal, Open arc, Compass rose, Heart, Tile (a picture of any
  channel) and tiles whose icon moves with the value (speed, elevation, heading, grade, cadence).
  A gauge starts on the channel it was made for; imported type names are mapped to ours
- ✅ Batch 2, rides first (2026-11-14): Ride cluster (compass strip, elevation, grade, a rider who
  leans with the grade, pedals at the real cadence and whose wheels turn with the speed; cadence and
  elevation arcs, power, distance), Graph, Graph pair (with heart rate), Progress (the same rider
  from start to finish), Climb wedge, Column glass, Crank (pedals at the real cadence), Route map
  arrow and Lap map arrow (pointing the way), Half dial left and right, Tape elevation, G-force
  rings (±0.5 g, one scale for both axes). A moving part can run over the recording's own lowest
  to highest value (`"range": "recording"`); an animation strip follows its own value range
- ✅ Batch 3 (2026-11-14): Side arc right and left, Crown arc, Compass ring, Heading tape, Readout
  right, Lap time right (minutes:seconds), Slant readout (with a striped fill), Pill readout, Accel
  bar (braking to the left, speeding up to the right), Thermometer, Heart pulse (a ripple for every
  real heart beat), Odometer (the higher wheels turn on only at the carry, so the digits read
  clearly), Circle readout, Ring readout, Column scale, Dome. The preview draws the moving parts
  of a gauge smooth on OpenGL too
- ✅ Batch 4 (2026-11-14): Dial notched, Dial pair (speed and cadence), Half round, Segment ring,
  Dial fill, Open arc thin, Graph window, Field box, Label plate, Grid level, Tile bar, Skid ball,
  Horizon (moves with the slope), Dial g (a fixed -3..5 g scale), Crown arc centerd and Side arc
  centerd (0 in the middle, ±0.5 g), Pedal offset left and right, Power phase left and right, Pedals,
  Ride cluster power (heart rate, both pedals, each leg's power in dashes, L | R balance). Every
  imported gauge now has one of ours doing its job; the others are covered by earlier ones. The demo
  ride has pedal data and vertical g, and gauges imported before a channel name was mapped get it
  when loaded
- ✅ Templates from the own set (2026-11-14, `tools/vectorgauges/templates.py`, 29 in all): Cycling -
  Ride, Ride cluster, Ride strip, Ride tiles, Steps, Heart and cadence, Speed and cadence, Twin dials,
  Columns, Power, Pedaling, Mountain pass; Simple - Minimal, Speed, Pills, Slants, Rings, Arcs,
  Lines; Motorsport - Track day, Lap timer, Race, G-force, Dashboard; Outdoor - Climb, Hike, Descent,
  Field boxes, Instruments. The script reports gauges that overlap or leave the frame. Laid out for
  the data we read today; engine, boat and aircraft layouts wait for those channels
- Own gauge set, vector: every imported raster gauge gets an SVG gauge of our own covering the same
  job (speed dial, tape, G-plot, lap timer...), with open fonts and clear names, and templates are
  rebuilt from them. Nothing of an imported file goes into the repo. Reviewed one by one on a
  local page showing the imported gauge and ours side by side with the same sample data
- ✅ Built-in gauge editor, first version (2026-11-15, `GaugeEditor`, from the library panel: "New
  gauge..." and "Edit..."): the parts as a list, a live picture with the sample ride where they are
  picked, moved and sized with the mouse, and the picked part's properties. Parts: texts, pictures
  (SVG and bitmaps), rectangle, ellipse, arc, pie, line, needle, filling bar and arc, round and
  sliding scales, graph, route and lap route (with what is drawn at the current point), groups.
  One movement per part (turn, fill, slide). Gauges that come with the program open as a copy
- ✅ Editor (2026-11-15): undo and redo; a part being moved is drawn to the edges and middles of
  the others and of the gauge; fonts by name; a second click picks inside a group
- Editor, still to do: a part's second and later movements, the g-ball and the picture strip;
  markers of a graph's axes and a lap's start line; gradients; the points of a line with more
  than two; aligning several parts; turning a text's frame into a box
- Path shapes; G-plot with a tail, lap table, sector bar

## M7 — Polish and distribution
- Export backend B (in-process libav) if/when needed; overlay-only alpha export; export queue
- AppImage + Flatpak, Windows installer, macOS bundle
- Translations (uk, en), user manual
- Shorter CI once the features have settled: the build takes about 8 minutes and the tests longer;
  decide which tests run on every push and which only before a release

## M8 — Touch UI
- QML app on `rtvcore` for tablets/phones (needs backend B)

## Later / maybe
Multi-camera picture-in-picture, side-by-side lap comparison with ghost, track database with
auto-detected start/finish, 360 video, live OBD.

**Command line (`trackrideviz --render project.rtvproj -o out.mp4`, and video + data + template
without a project).** Cheap once M2 exists, because project loading and `ExportJob` live in the
core and need no window: it is the same binary started with `QT_QPA_PLATFORM=offscreen` logic or a
`QGuiApplication`. Plan it right after M3 (templates), when there is something worth batch-rendering.
