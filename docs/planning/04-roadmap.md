# 4. Roadmap

Each milestone ends with something usable; nothing is built "for later" before it is needed.

## M0 - Repo skeleton ✅ (2026-11-12)
Repo, README, GPL-3.0, qmake project, main window shell with docks, en/uk translations,
`Channel` + first unit test, debian/spec/macOS/Windows packaging and both workflows.
Verified: local build + tests (Qt 6.11), deb build + tests in an Ubuntu 22.04 container (Qt 6.2.4).
Not yet verified (needs a push to GitHub): AppImage, macOS and Windows jobs.

## M1 - First overlay on screen (vertical slice) ✅ (2026-11-12)
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

## M1.5 - Settings ✅ (2026-11-12)
- `AppSettings` in the core; settings dialog with Appearance (style, light/dark, language, window
  layout), Preview (hardware decoding, preferred decoder) and Units (speed, length, temperature)
- Channel table and the built-in speedometer follow the units
- Where each kind of setting lives: [03-architecture §3.7a](03-architecture.md)

## M2 - First exported video ✅ (2026-11-12)
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

## M3 - Real gauges

### M3a - gauge engine, library, editing ✅ (2026-11-12)
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

### M3b - every `.veg` part, templates, speed ✅ (2026-11-12)
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

### M3c - what is left
- ✅ G-force comes from GPS now (M4); without data a part rests at 0, not at its scale's start
- ✅ Fonts named by imported gauges (2026-11-18, `GaugeFonts`): Roboto, Roboto Condensed ("Roboto Cn" of .veg
  gauges), Orbitron in place of Furore and DSEG7 Classic in place of DS-Digital ship with the program (OFL);
  Consolas and Courier New fall to a monospaced font, Arial, Helvetica, Verdana and Tahoma to their usual
  stand-ins, Impact to Roboto Condensed - each only where the computer lacks the font itself. Gauges imported
  earlier keep "Roboto" for "Roboto Cn" until imported again
- ✅ The "nav" knob of track line and lap circuit gauges (an arrow turned by the heading, on the route's point)
  points the way of travel since heading is worked out from GPS (M4); checked again 2026-11-18 at three moments
  of the sample ride
- ✅ Gauges follow the video's shape (2026-11-17, `Project::layoutAspect`): the project keeps the shape its
  gauges were placed on; a video of another shape (a vertical one after 16:9) gets each placed again from
  its nearest edges, as a template places them, as one step that can be undone
- ✅ Saving the gauges on the video as a template (2026-11-14, File menu): each measured from
  the edge it is nearest to, with a name and a category
- ✅ A gauge's own shape on the video (2026-11-17, `GaugeShape`, `OverlayItem::aspect`): "Keep the gauge's
  shape" off in its menu gives handles on the right and bottom edges; the gauge is laid out again for the
  new width (frames, points, pivots, tape travel spread over it; letters and markers at the current point
  as they were), so a profile or a graph can run across the picture; round parts become oval. Kept in
  projects and templates
- ✅ Units a single gauge shows (2026-11-15): "Units" in a gauge's menu on the video - as in the settings,
  metric or imperial; kept in the project and in templates

## M4 - Track day features
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
- ✅ Cuts (2026-11-13, `CutList`): parts of the video left out of the export, marked A-B on the
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
- ✅ GoPro data kept in a join (2026-11-17, `VideoJoin::keepsCameraData`): when every part has a GPMF
  track and the output is .mp4/.mov, it is joined with the picture and sound, so the joined video
  carries its own data. Checked with GoPro's sample files (HERO5, HERO6 joined with themselves:
  twice the positions, twice as long)
- ✅ GoPro videos without a satellite fix (2026-11-17, `fromMotionSensorsAlone`): read for their
  motion sensors. Up from gravity, right as the level direction that follows the turns the
  gyroscope feels, ahead across the two and the way that brakes harder. Checked with made-up rides
  (both ways of mirrored axes; a leaning camera gets none); GoPro's samples load (HERO7, HERO8,
  Karma), but are handheld and walked, so they do not tell how good it is on a vehicle
- ✅ Joining videos of different kinds (2026-11-17, `VideoJoin::encodeArguments`): when they cannot be
  copied, the dialog says why and offers to encode them again: the first one's size (the others
  scaled in, black bars where the shape differs) and frame rate, 48 kHz stereo AAC with silence for
  parts without sound, each part's sound cut to its picture so no frame goes missing at a seam;
  the last export's encoder at the first video's bitrate; a progress bar from ffmpeg's -progress

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
- ✅ A map of its own per gauge (2026-11-17, `TileStore::forMap`): "Map" in a route gauge's menu on the video
  and in the editor, as in the settings or one of the maps the program knows; each map has a store of its own,
  following the settings' downloads, credit and key. Kept in gauges ("map"), projects and templates
- ✅ A coarser tile stands in for a missing one (2026-11-19, `TileStore::picture`): up to 8 zooms coarser, enlarged -
  while the tile comes, and for good where the server has nothing so close (IGN outside France, USGS outside the USA:
  over Kyiv IGN's map ends at zoom 7, its photos at 11, USGS at 7); once a tile is refused, the coarser one is fetched
- ✅ More maps and a key per service (2026-11-18): free OSM Humanitarian and France, ÖPNVKarte, Esri topographic and
  streets, maps-for-free relief, IGN France map and photos, USGS topo and imagery (each checked with a real tile);
  Thunderforest, Stadia (with Stamen), MapTiler and Mapbox styles with the user's key (addresses as the services
  document them, untried without a key), offered once their key is entered; the one key of before becomes
  Thunderforest's. CARTO was left out: its tiles now want a key and come back as "API key required" pictures
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
- ✅ Files dropped on the window (2026-11-17): a project opens; a video opens (chapters offered to be joined);
  data is read; gauges, templates and folders of them are imported into the library
- ✅ Gauges and templates dragged from the library onto the video (2026-11-17): a gauge is placed centered where
  it is let go, kept on the picture; a template is applied
- ✅ The library panel finds gauges and templates by name and by group (2026-11-14): where they
  come from, what a gauge shows (maps, speed, laps, ...), a template's category
- ✅ The close-up turned so that the way ahead points up (`"headingUp"`, in the same menu); a
  question before an export while map tiles or terrain heights are still being fetched; a button
  in Settings > Maps that deletes the stored tiles and says how much they take (2026-11-14)

## M5 - More data
- ✅ GoPro embedded telemetry (2026-11-15, `GpmfImporter`): "Open data" on the camera's own .mp4
  reads its positions, elevation and speed (GPS5 and GPS9), on the video's clock, so nothing is
  to be synchronized. Checked with made-up files only: no camera file was at hand
- ✅ GoPro chapters (2026-11-16, `VideoJoin::chaptersFrom`): opening the first chapter offers to join the
  recording; the data is read on through the chapters after it, also for a video joined from them;
  a GoPro video opened without data gives its own
- ✅ GoPro accelerometer and gyroscope (2026-11-16, `GpmfImporter`, `DerivedChannels`): the G-forces
  measured, turned from the camera's axes to the vehicle's (up from gravity, forwards from the changes of
  speed, right from the turns), in place of those worked out from GPS when they go with the ride; a camera
  that leans into the turns gives the lean angle from its gyroscope and has its readings turned back level
- ✅ CSV with data profiles; XML data profile import (2026-11-13)
- ✅ AiM and Racelogic (DriftBox, PerformanceBox) CSV layouts (2026-11-17, `DataProfile::Layout`)
- our own data profiles for common loggers: ✅ TrackAddict (2026-11-17, checked against two real logs);
  RaceChrono, Harry's LapTimer, Torque wait for sample files
- ✅ NMEA and VBO (2026-11-16, `NmeaImporter`, `VboImporter`): RMC and GGA sentences, checksums checked,
  .txt and .log read as NMEA when they are; VBOX files by their column names, other columns (rpm...) kept
- ✅ Gears (2026-11-15, `Gears`, the Bike page of the settings, the "Gear" gauge): recorded by
  electronic shifting (FIT gear change events), or worked out from the speed and the cadence with the
  chainrings, the sprockets and the wheel entered once; the last gear held while coasting
- ✅ A cassette drawn with the gear in use lit and the chain sliding over at a shift ("Cassette"; the
  `gears` part of gauges)
- ✅ TCX (2026-11-15, `TcxImporter`): track points of every lap with heart rate, cadence, speed and
  power; the file's laps when it has more than one
- ✅ Multiple data sources per project (2026-11-16, `DataSources`, File > Add data from other files):
  channels of other files (a chest strap, a power meter) added to the main data, lined up by the
  clocks or shifted by hand; a channel both have taken from the added file only when chosen
- ✅ Power estimated for rides without a power meter, shown as an estimate,
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

## M6 - Gauge editor
- ✅ SVG drawings in gauges (2026-11-13): sharp at any size, theme colors inside the drawing;
  Barlow Semi Condensed (OFL) bundled for gauge texts
- ✅ `.rtvgauge` / `.rtvtemplate` (2026-11-13): one gzipped JSON file to pass a gauge or a template
  with its gauges on; Export and Import in the library panel. Built-in gauges and templates marked
  with a padlock. File associations: in the Windows installer (2026-11-17); on Linux (2026-11-18) MIME types in the
  .deb and .rpm (`trackrideviz-mime.xml`, named in the desktop file and the AppStream metainfo); the AppImage carries
  them too, for whatever integration tool takes it in (untested)
- ✅ Project font (2026-11-13): font, weight and "also gauges with a font of their own" next to the
  theme colors, kept in projects and templates; Barlow Semi Condensed bundled in all weights
- ✅ Per gauge on the video (2026-11-17, `OverlayItem::ownTheme`, `textScale`, `opacity`): text size and opacity
  in the properties; "Own colors and font" makes the color and font controls under it edit that gauge alone.
  A see-through gauge is drawn whole, then faded, so its parts do not show through each other. Kept in
  projects and templates
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
- ✅ Editor (2026-11-17): a part's second and later movements, applied in their order ("Movement 1",
  "Movement 2", Add another movement); a ball moved by two values (g-forces: the measurement each way, how far,
  either way reversed, the trail) and a picture strip (how many pictures, how fast at the ends)
- ✅ Editor (2026-11-17): fills that fade to another color along a line; lines through any number of points,
  closed and filled as polygons ("filled" on a line in gauge.json); a text in a box, wrapped at its width
- ✅ Editor (2026-11-17): a graph's markers on its top and left edges and a lap map's start line, each a list
  under a heading of its own below the part (what is drawn at the current point under a third)
- ✅ Editor (2026-11-17): several parts picked (Ctrl or Shift, in the list or on the picture), moved together
  and lined up: left edges, centers, right edges, tops, middles, bottoms, spread evenly across or down
- ✅ Path shapes (2026-11-17): polygons, and lines and polygons curved smoothly through their points ("smooth")
- ✅ Lap table and sector bar (2026-11-17, gauge parts `lapTable` and `sectorBar`; gauges Lap table and
  Sector bar; in the editor). The table: the last laps finished and the one under way, number, time and
  how far behind the best so far, the best lit. The bar: the sectors of the lap under way, each finished
  one lit when it beat the best of it in the laps before, the one being driven filling up against that
  best; empty segments without sector lines
- ✅ G-plot with a tail and Lean angle (2026-11-16): `trailSeconds` of a two-value move draws the way
  the part went over the last seconds, fading; a lean-angle gauge with a rider leaning under an arc
- ✅ For runners (2026-11-16): pace as minutes and seconds (5:18 /km) everywhere; the cadence of FIT runs
  and walks in steps a minute; "Run cluster" (the ride cluster with a runner striding at the cadence,
  the pace, heart rate, cadence and elevation arcs) and "Pace" (the pace large beside a running figure)

## Sound and export choices ✅ (2026-11-15)
- ✅ Music dock: files played one after another from a moment of the video, each trimmed, with
  fades; volumes of the video's sound and of the music; heard in the preview and mixed into the
  export; kept in the project (paths only)
- ✅ Opus and Vorbis sound (in MKV), and its bitrate
- ✅ Export profiles: YouTube upload, small file (HEVC + Opus in MKV), like the video, and one's own

Since (2026-11-16): the music lowered while the camera is loud (ducking, in the export); AV1
encoders, preferred by the Small file profile; volumes up to 200 %, held under full scale by a limiter;
the automatic end of a fixed dial following the highest value held for a second, not a GPS spike.

## M7 - Polish and distribution
- ✅ The program's own video engine (2026-11-19, `Libav`, `LibavExport`, version 0.04): FFmpeg's libraries (6.1 or
  later) linked in when present (`src/core/libav.pri`). The export plan is kept in parts (inputs, filter graph,
  sound, options) and the ffmpeg command line is made of them; LibavExport runs the same parts in-process - same
  filter graph (set up in steps, so VA-API's upload filter gets its device), same encoders and options, the gauges
  handed over as pictures, copied or encoded sound, a stop that keeps a playable file with sound of the same length.
  Videos are read through it (ffprobe only as a fallback); encoders are found through it (hardware ones by a test
  encode). Settings > Export > Video engine: built in (default) or the ffmpeg program; the other is used when the
  chosen one has no encoder; `--engine` on the command line; the export window names the engine. Checked: every
  export test runs on both engines; x264 output bit-identical to ffmpeg's; VA-API and Quick Sync encode. CI builds it
  everywhere: FFmpeg 8.1.2 built for the AppImage, MSYS2's on Windows, Homebrew's on macOS, ffmpeg-free on Fedora;
  the Ubuntu 22.04 .deb stays on the ffmpeg program. Joining videos (copied: `Libav::copyJoin`; encoded: the
  join as an `ExportPlan` run by LibavExport; both through `VideoJoinJob`) and the music's lengths follow the chosen
  engine too, as does reading videos (the other engine where it fails) (2026-11-19). An export queue was dropped: several projects are exported one after another by the command line
- ✅ Command line (2026-11-17, `CommandLine`, `ProjectData`): --render a project, or a video, data and a template
  (synced by the clocks or --offset); --profile, --encoder / --hardware / --software and each choice of the export
  dialog by hand; --gauges-only; --list-encoders, --list-profiles, --list-templates; progress in a terminal, a line
  every tenth into a file; Ctrl+C keeps a playable file. Waits for the terrain, the weather and the street maps.
  Linux checked; Windows (its console taken from cmd) not yet
- AppImage, macOS bundle (macOS back in CI 2026-11-18, ffmpeg from Homebrew); no Flatpak
- ✅ Windows installer (2026-11-17, `windows/trackrideviz.nsi`, NSIS, built by CI next to the zip): for the user
  alone or for everyone, Start menu and desktop shortcuts, .rtvproj / .rtvgauge / .rtvtemplate opened by a
  double click; the uninstaller takes away only what was installed (an update first runs the old one).
  English and Ukrainian. Checked under Wine: install, update over it, uninstall
- Translations (uk, en); ✅ user manual (2026-11-18, `docs/manual.md`, Help > User manual): every part of the
  program, the files and where they are kept, the command line, updates, and how to read the logs by hand
- Updates (2026-11-17, `ReleaseCheck`): ✅ a release build carries its tag (CI, RTV_RELEASE_TAG; other builds never
  ask); once a day, quietly, GitHub's latest release is compared with it; a newer one shows as a link in the status
  bar and in Help, with its notes, the release page, "Skip this version"; Help > Check for updates; a switch in
  Settings. ✅ "Update now" (`Updater`): the AppImage downloads the new one beside it, checks it against GitHub's
  SHA-256 and renames it over itself, then offers a restart; a copy the installer put there (told by the installer's
  registry entry pointing at its folder) gets the new setup.exe, checked the same way, and runs it after closing:
  quietly with /RESTART for the user alone, with its pages for everyone; the installer waits for the old program to
  end. The zip, macOS and distributions' packages only point to the release page. ✅ .zsync (2026-11-18,
  `tools/appimage/set-update-info.py`): the AppImage keeps its name without a version; the update information in it
  names that, and its .zsync goes to the release beside it, so AppImageUpdate, appimaged, Gear Lever and
  AppImageLauncher update it from the latest release, fetching only what changed
- ✅ Shorter CI (2026-11-18): Windows installs only the Qt modules used, not all of Qt (3.4 GB); the .deb is
  built by its own rules, without the .buildinfo (over a minute) and the debug symbols package that were never
  uploaded; the tests job builds the core and the tests only, the program being compiled by the release workflow

## Next (agreed 2026-11-21, after comparing with other overlay tools)
In order:
- ✅ Speed-dependent map zoom (2026-11-21, `GaugeComponent::SpeedZoom`): "Custom..." and "Dynamic, by
  speed..." in the close-up menu, each asking for its values; the close-up's meters across go from the
  slow one to the fast one in proportion to the speed averaged over ±3 s; the line keeps its width; the
  tiles of every zoom level in between are fetched ahead, so an export needs no connection. Kept in
  projects and templates
- ✅ Video from data alone (2026-11-21, `VideoBackground`, `DataClock`/`Playback`, File > Without a video...,
  `--background`): the gauges over a plain color or see-through, at a chosen size (landscape, upright,
  square, 4K, 720p) and frame rate, as long as the data and on its own time (nothing to synchronize).
  The preview plays on a clock of its own in place of the video player; the export draws over a color
  source (the gauges input 0, music after); saved in projects. A Strava-like look comes from a big map
  (the 3D flyover is next)
- ✅ 3D terrain flyover, first part (2026-11-23, `Flyover`, a route map's "3D flyover"): the land around
  the rider from the terrain's heights (x1.5), the gauge's map draped over it, hill shading from a low
  north-west sun, haze into a sky; the camera as route videos have it (60° from straight down,
  100 m up, looking at the rider), its heading from the way over enough seconds that stops and
  GPS wander do not turn it; the route ridden in the gauge's color, the rest faint; the gauge's
  marker on the rider. Our own software rasterizer (depth buffer, perspective-correct texture, one
  band of rows per processor): the same in the preview, the export, the command line, tests and
  Wine; about 35 ms a 1080p frame on a 4-core laptop. Land worked out again every 0.6 span; its
  terrain and map tiles fetched ahead along the route; the export waits for terrain tiles too.
  The camera's heading summed over 15 s either way, weighted to the middle: no jumps (2026-11-23).
  Hill shading only on slopes, from slopes steepened x2.5 for the light, flat land keeping the map's
  colors; haze growing with distance to the power 1.6 (2026-11-23).
  The camera's tilt, height and the hills' height as settings: "3D flyover camera..." in the menu,
  in the gauge editor, kept in gauges, projects and templates (2026-11-23). A terrain tile on its way
  or refused is no longer looked for on the disk, and a patch with tiles missing is worked out again
  once a second, not for every picture: without terrain a flyover cost seconds a picture
- ✅ Time factor (2026-11-23): a time-lapse video's speed-up set by hand (**File > Time-lapse video...** x10, x30, `SyncMap::rate`, `--timelapse`; data time = video time x rate + offset; the clock match
  covers the video's length times the rate), and an export played faster (**Faster** x2, x4 in the
  export dialog, `ExportSettings::speedUp`, `--faster`: `setpts` and `fps` after the cuts, the frames
  in between left out, each picture's gauges at its own moment, the camera's sound left out, music as
  usual); the same on both engines
- ✅ Data editor, first part (2026-11-23): **Cut video** in the Data panel makes the stretch picked on
  the chart or in the table a cut of the video (at the video's moments, time-lapse included), the data
  staying whole for the sync; **File > Save data as...** writes the data with its corrections as GPX
  (track, heights, times; heart rate, cadence, power, temperature, speed in the extensions) or CSV
  (every channel, every sample, the clock; read back by the built-in "Race Track Visualizer" data
  profile, whose "*" takes every column by its name) (`TelemetryWriter`). Laps are not saved
- ✅ GPS problems (2026-11-23, `GpsProblems`, the Data panel's **GPS problems**): gaps (no position
  for 10 s and 4 usual intervals at least, while moving 2 m/s or more across it; not a pause),
  jumps (positions the last good one could not reach at 3x the speed 90% of the ride is below, and
  20 m/s at least, that go more than 30 m off the line between where they left and came back; a
  run longer than 60 s is taken as the way), frozen positions (still under 1 m a sample for 5 s
  while a recorded speed says over 3 m/s). Jumps and frozen stretches fixed by taking the
  positions out (DataEdit), gaps read straight across; none found again once fixed. Filling along
  the road (routing) later, if wanted
- Graph gauges over distance as well as time (an elevation profile)
- Reference lap: a gauge of time ahead or behind the ride's best lap; other rides' laps (a track
  database) later
- Per-lap export: one video per lap, or per chosen lap

## M8 - Touch UI
- QML app on `rtvcore` (backend B, the built-in video engine, is done): the only UI on Android; on the
  desktop a second way in, by its own icon (a .desktop entry / Start menu shortcut) and a command-line
  switch, beside the full app
- Few screens, most things automatic: video (chapters joined), data (from the video or a file, synced
  by the clocks), look, export with presets. Video and data adjusted by hand on a preview frame
  (always needed). A long tap opens a menu leading to lists: gauges, templates, music

## After the simple UI: GoPro
- GoPro GPS clock sync: the camera's GPS time matched to the data's by itself (the most common complaint
  elsewhere: video and data out of step)
- TimeWarp: the factor read from the camera's data, a speed ramp varying within a video

## Later / maybe
Multi-camera picture-in-picture, side-by-side lap comparison with ghost (better still: a 3D rider
drawn over the flyover, racing the ghost; complicated), track database with auto-detected
start/finish, 360 video, live OBD.

**Command line (`trackrideviz --render project.rtvproj -o out.mp4`, and video + data + template
without a project).** Cheap once M2 exists, because project loading and `ExportJob` live in the
core and need no window: it is the same binary started with `QT_QPA_PLATFORM=offscreen` logic or a
`QGuiApplication`. Plan it right after M3 (templates), when there is something worth batch-rendering.
