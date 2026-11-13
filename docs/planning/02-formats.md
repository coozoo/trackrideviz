# 2. Formats — what we read, what we write

Principle: **import, don't open-in-place.** Foreign gauges/templates/profiles are converted once into
our native format and stored in the user's library. We never write the foreign formats back.

## 2.1 Gauge and template formats

### `.veg` (gauge) — JSON, fully understood, import is straightforward

```
{ frame:{width,height}, supportedTypes|unsupportedTypes:[...], unlisted, isLogo,
  components:[ ... ] }
```

Component `type`s seen across the 256 shipped gauges (count):

| Type | n | Meaning |
|------|---|---------|
| `image` | 982 | PNG at `frame`, tinted by `colorStyle`; optional `manipulation` |
| `text` | 838 | `autoTextType`: `value`, `unit`, `valueAndUnit`, `label`, `labelValueUnit`, `intervalMarking`; or fixed `textValue`; `fontFamily`, `fontSize`, align, `valuePrecision` |
| `dynamicRotate` | 94 | manipulation: rotate around `pivotPointX/Y` from `minAngle` to `maxAngle` (needles) |
| `dynamicRadialClip` | 81 | manipulation: reveal by angle (arc fill) |
| `dynamicTranslate` | 63 | manipulation: slide between `minX..maxX` / `minY..maxY` (tapes, sliders) |
| `dynamicWidthClip` / `dynamicHeightClip` | 32 / 21 | manipulation: bar fill |
| `group` | 45 | nested components, shared manipulation |
| `graphLine` | 17 | history graph with `lineWidth`, `bottomFillColor`, x/y axis marker and line-marker sub-components |
| `trackLine` | 14 | track map: `thickness`, `trackKnobComponents`, `startLineComponents`, `padding` |
| `animationStrip` | 12 | sprite sheet: `numberOfFrames`, `frameRect`, min/max fps driven by value (beating heart) |
| `lapCircuit` | 10 | lap progress |
| `staticRadialClip` | 1 | fixed mask |

Other keys: `auxiliaryType` (a component bound to a *second* metric — combo gauges),
`minValue`/`maxValue`/`wrapValue`/`minInterval` (scale), `colorStyle`, `fontColor`,
`hideWhenUnknown`, `clipAngle`, `invertXAxis`/`invertYAxis`, `casingTransform`.

### `.vet` (template) — JSON, importable

```
{ category, thumbnailPath, backgroundColor, accentColor, accent2Color, activityDefault,
  widgets:[ { filename:"x.veg", valueType:"speed",
              origin:{ x:{offset,fromEdge:"min"|"max"}, y:{...} }, scale? } ] }
```
360 templates add `sphericalPosition`/`sphericalTransform` — ignored.

**Asset caveat:** the gauges reference Garmin's PNGs (`Widgets/images`, 394 files). We do not ship
them. The importer asks for a folder of the user's `.veg` / `.vet` files (or one holding `Widgets` and `Templates`) and
copies images into the user's own library.

### XML gauges — imported (2026-11-13)

A folder with `gauge.xml` and its pictures. `<Gauge version="3" width height transparency>` →
`<Components>` (nested; a child's `x`, `y` count from its parent's) → `<Inputs>`: per gauge, input
names → data types (`Speed (MPH)`); per part, its input slots (`Value`, `X`, `Y`, `Lateral G`...)
→ those names. All 185 gauges of a full installation are version 3. Drawn rather than painted.

`XmlGaugeImporter` converts on import:

| Part | Becomes |
|------|---------|
| Rectangle, Ellipse, Arc, Pie (outlined or filled) | `Shape` |
| Bar (gradient, four directions) | filled `Shape` with a gradient + width or height clip |
| Needle, NonLinearNeedle | `Shape` line + `Rotate` (non-linear: a value curve) |
| ImageNeedle, NonLinearImageNeedle, Image | `Image` (+ `Rotate` about the axis point) |
| AnimatedArc / Pie, NonLinear… | `Shape` arc / pie + `RadialClip` |
| AnalogFace | `Dial` (arc, minor/major ticks, numbers); parts inside may take its value or angle range |
| TickerTape | `Tape` |
| Text, DynamicText | boxed `Text`; integer (cut), decimal, running time, degrees-minutes-seconds, time of day |
| Map (+ MapDot, MapPositionImage, a Map inside a Map) | `TrackMap` with knobs |
| ChartComponent | `Graph`, optionally a window that moves along (seconds back / forward) |
| GPlot | axes, rings and numbers as shapes, a ball on `Translate2D` |

Angles count clockwise from 3 o'clock (ours from 12: +90°; a picture's own turn stays as it is).
Font sizes are points at 96 dpi. Every colour has its own `transparency` (opacity), multiplied by
the gauge's. A text at a point is placed beside it on the side its alignment names; a text with a
rectangle is aligned inside it. A gauge is made for a data type in a fixed unit: the parts keep
that unit (`GaugeComponent::unit`), so a needle matches the printed numbers.

Gauge files of version 2 (still found inside templates) are the same except that some data types
lack their unit, which was always the same: positions and distance in feet, speeds in mph,
temperatures in °F, pressures in psi. The importer adds it.

Left out, and reported: Digit (picture per digit, 1 gauge), the G-plot's stick and tail, chart
axis labels and grids, charts over distance (drawn over time), values from the fastest lap
(ghost), vertical text.

### XML templates — imported (2026-11-13)

A folder with `project.cdp` (`<Project version="2">`) and `Gauges/<name>/` holding a copy of each
gauge it uses. `<GaugeSettings>` lists `<Gauge name x y transparency>` in drawing order: the top
left corner in pixels of the video, at the gauge's design size. The rest (titles, video profile,
data input mappings, chart tabs) is not used.

The file does not state the frame size. `XmlTemplateImporter` takes it from the template's name
("720p", "1080p"), otherwise the smallest of 720 x 480, 1280 x 720 and 1920 x 1080 that holds every
gauge (all 20 of a full installation come out as their names and video profiles suggest). Each gauge
is anchored to the edge or middle nearest its centre. A template's copy of a gauge that differs from
the library's gauge of that name (templates for 720p carry smaller copies) is stored as
"<gauge> - <template>". A gauge's `transparency` in the template is not used (noted).

### Binary, undocumented gauge formats — no import planned.

### `.vet` placement, as found (2026-11-12)

Offsets are pixels of a 1920 x 1080 frame, measured from the edge named by `fromEdge`: `min`
(left/top), `mid` (centre) or `max` (right/bottom), to the gauge's left/top side; gauges are at
their design size (`scale` is in the format but no shipped template uses it). Checked: 196 of the
right/bottom-anchored entries are exactly flush with that edge. An entry may be a group
(`origin` + `widgets`), whose members' plain `{x, y}` origins are offsets from the group's. Logos
are named like any gauge. Colours: `backgroundColor`, `accentColor`, `accent2Color`; there is no
text colour - we use white, black on a light, mostly opaque background.

Rendering details inferred from the files and the artwork (the renderer is native code):
graphs span the whole recording; markers' origins are the current point, the top edge at the
current x, the left edge at the current y; `bottomFillColor` is often alpha only (the line's
colour); track knobs and lap start lines have their origin at the point; a positive `clipAngle`
leans a horizontal bar's edge right at the top and tilts a vertical fill's edge up to the left;
`staticRadialClip` is a fixed mask, not a component; `animationStrip` frames run along the
strip's longer side.

### Native gauge: a folder with `gauge.json` + images (implemented)

`GaugeFile` - every `.veg` part has its own field; images by relative path inside the folder.
Kept in the user's gauge library (`<app data>/gauges/<name>/`).

### Native template: one JSON file (implemented)

`TemplateFile` - name, category, theme, and per gauge: library id, channel, edge and offset for x
and y in 1920 x 1080 reference pixels, height. Kept in `<app data>/templates/`.

### Native, still planned

Additions to the `.veg` component model:
- ✅ vector shapes (rectangle, ellipse, arc, pie, line), dials, tapes, non-linear scales, value
  formats, a unit per component (for XML gauges)
- a path shape; per-component input transform
- extra components: g-plot with a tail, lap table, sector bar, 7-segment text style
- SVG as well as PNG images
- a gauge as one zip file for sharing

### XML data profiles — imported (2026-11-13)

`<DataProfile version="2">`: header line indices (several are joined "name - unit"), data start line,
separator characters, decimal comma, file line matchers (line, text, contains / exact, case),
calculators (input columns → output columns) and column mappings (column text, exact / contains,
case → data type). `XmlDataProfileImporter` converts them to our JSON profiles
(`<app data>/dataprofiles`); `.csv`, `.txt` and `.log` files are read with the first one that
recognises them.

Cells, as found: numbers (decimal comma when set), clock times and dates (seconds from the first in
the column), mm:ss.fff and 1' 23" 456, hemisphere letters (N/E = 1, S/W = -1, "12.5S" = -12.5).
"Data Running Time, Seconds" is the time base; "Time of Day" and "Date" give the start (taken as
UTC); "Lap Indicator" starts a lap wherever it changes to anything but 0.

Kept as calculators: unit converters (distance, speed, temperature, pressure — relative to the
atmosphere if asked —, angle and grade, pulses to RPM), math (+ − × ÷, √, power, modulus, log,
hypotenuse, trigonometry, inverse; with a constant or a second column), gap filling with outlier
removal, time-window averages (plain or of angles), deltas, last value. Left out and noted:
calculators that derive measurements (lap statistics, ascent/descent, heading and distance from
positions, G from speed, speed from distance; for every source alike in M4), positions projected
to feet or metres. Refused: AiM and DriftBox layouts (header found by blank lines / sections), and
profiles without line matchers. Of a full installation's 54: 49 import.

## 2.2 Telemetry formats

| Format | Source | Approach | Phase |
|--------|--------|----------|-------|
| GPX (+ Garmin TrackPointExtension: hr, cad, temp, power) | everything | `QXmlStreamReader`; start from Overseer's `GpxTrack` and widen it | 1 |
| FIT | Garmin, Wahoo, action cameras, most bike computers | Own decoder (the format is a simple record stream; avoids FIT SDK licence terms) or the official C++ SDK — see decisions §5 | 1 |
| GoPro GPMF (embedded in MP4) | GoPro Hero 5+ | Demux the `gpmd` data stream via libavformat/ffprobe, parse KLV ourselves (GPS5/GPS9, ACCL, GYRO) | 2 |
| CSV + data profile ✅ | RaceChrono, Harry's LapTimer, TrackAddict, Torque OBD, anything | Profile = matcher + separators + column mappings + calculators (`CsvImporter`, `DataProfile`). XML data profiles imported; own profiles to ship later | 2 |
| TCX | older Garmin, Strava export | XML | 2 |
| NMEA 0183 | raw GPS loggers, dashcams | line parser (RMC/GGA/VTG) | 3 |
| VBO | Racelogic VBOX, RaceChrono export | text, header-described columns | 3 |
| DJI `.SRT` | drones | subtitle-embedded telemetry | 3 |
| Sony / Insta360 embedded | | see Gyroflow telemetry-parser | later |

All importers produce the same thing: a `TelemetrySession` (see 03-architecture §3).

## 2.3 Channel catalogue

Internal canonical channels in SI units (m, m/s, s, rad or deg, Pa, K, W); display units are a
presentation setting. Names follow the `valueType` identifiers of `.veg` files where one exists (`speed`,
`heartRate`, `engineRpm`, `gForceLat`, `currentLapTime`…) so template import is a direct mapping.
Unknown CSV columns become user channels with a free-form name and unit.

## 2.4 Project file: `.rtvproj` (JSON)

```
{ version, canvas:{width,height,fps},
  media:[ {id, files:[...chapters], trimIn, trimOut} ],
  data:[ {id, file, profile?, syncOffsetMs, syncMode:"utc"|"manual"} ],
  derived:[ {channel, calculator, params} ],
  track:{ startFinish:{lat,lon,heading,width}, sectors:[...], closedCourse, ... },
  theme:{background, foreground, accent, accent2}, units:{...},
  overlays:[ {gauge:"library-id or embedded", channel, anchor:{x:{offset,fromEdge},y:{...}},
              scale, opacity, visibleFrom?, visibleTo?} ],
  export:{ ...last used settings } }
```
Paths stored relative to the project file when possible. Versioned from day one with an upgrader
chain (other programs needed one too).

## 2.5 Output

- Composited video: MP4/MKV/MOV; H.264, H.265, AV1; software + hardware encoders; audio stream copy.
- Overlay only with alpha: ProRes 4444 / QuickTime Animation / VP9-alpha / PNG sequence.
- Stills: PNG snapshot of the current frame.
- Data: session → CSV / GPX (laps table too).
