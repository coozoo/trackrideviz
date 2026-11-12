# 2. Formats — what we read, what we write

Principle: **import, don't open-in-place.** Foreign gauges/templates/profiles are converted once into
our native format and stored in the user's library. We never write the foreign formats back.

## 2.1 Gauge and template formats

### the old editor `.veg` (gauge) — JSON, fully understood, import is straightforward

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

### the old editor `.vet` (template) — JSON, importable

```
{ category, thumbnailPath, backgroundColor, accentColor, accent2Color, activityDefault,
  widgets:[ { filename:"x.veg", valueType:"speed",
              origin:{ x:{offset,fromEdge:"min"|"max"}, y:{...} }, scale? } ] }
```
360 templates add `sphericalPosition`/`sphericalTransform` — ignored.

**Asset caveat:** the gauges reference Garmin's PNGs (`Widgets/images`, 394 files). We do not ship
them. The importer asks for the user's the old editor install folder (or a folder of `.veg` files) and
copies images into the user's own library.

### another editor gauge — XML, importable with moderate effort

`<Gauge name category width height version>` → `<Components>` (Needle, ImageNeedle, NonLinear*,
AnimatedArc/Pie, Bar, Digit, TickerTape, Dynamic/StaticGaugeText, shapes, Map*, Chart) →
`<Inputs>` / `<InputMapping dataFileIndex dataColumn>`. Three schema versions (V1→V2→V3 upgraders
exist in the original). Vector-drawn rather than image-based, so our model needs vector primitives
anyway. Priority: after the old editor import.

### another editor `.rro` / `.rrt` / `.rrp` — binary and undocumented, no import planned.

### Native: `.rtvgauge` / `.rtvtemplate` (JSON)

Superset of the the old editor component model with these additions:
- vector primitives (arc, pie, ellipse, rect, line, path) alongside images — from another editor
- non-linear scales (value→position table) — from another editor
- per-component input: channel + optional transform, instead of one `auxiliaryType`
- extra components: g-plot, ticker tape, lap table, sector bar, 7-segment text style
- SVG as well as PNG images
- a gauge is a folder or zip: `gauge.json` + `images/` — self-contained and shareable

## 2.2 Telemetry formats

| Format | Source | Approach | Phase |
|--------|--------|----------|-------|
| GPX (+ Garmin TrackPointExtension: hr, cad, temp, power) | everything | `QXmlStreamReader`; start from Overseer's `GpxTrack` and widen it | 1 |
| FIT | Garmin, Wahoo, the old editor G-Metrix, most bike computers | Own decoder (the format is a simple record stream; avoids FIT SDK licence terms) or the official C++ SDK — see decisions §5 | 1 |
| GoPro GPMF (embedded in MP4) | GoPro Hero 5+ | Demux the `gpmd` data stream via libavformat/ffprobe, parse KLV ourselves (GPS5/GPS9, ACCL, GYRO) | 2 |
| CSV + data profile | RaceChrono, Harry's LapTimer, TrackAddict, AiM export, Torque OBD, anything | Profile = matcher + separators + column mappings + calculators. Ship our own profiles; **import another editor profile XML** | 2 |
| TCX | older Garmin, Strava export | XML | 2 |
| NMEA 0183 | raw GPS loggers, dashcams | line parser (RMC/GGA/VTG) | 3 |
| VBO | Racelogic VBOX, RaceChrono export | text, header-described columns | 3 |
| DJI `.SRT` | drones | subtitle-embedded telemetry | 3 |
| Sony / Insta360 embedded | | see Gyroflow telemetry-parser | later |

All importers produce the same thing: a `TelemetrySession` (see 03-architecture §3).

## 2.3 Channel catalogue

Internal canonical channels in SI units (m, m/s, s, rad or deg, Pa, K, W); display units are a
presentation setting. Names follow the .veg format's `valueType` identifiers where one exists (`speed`,
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
chain (both reference apps needed one).

## 2.5 Output

- Composited video: MP4/MKV/MOV; H.264, H.265, AV1; software + hardware encoders; audio stream copy.
- Overlay only with alpha: ProRes 4444 / QuickTime Animation / VP9-alpha / PNG sequence.
- Stills: PNG snapshot of the current frame.
- Data: session → CSV / GPX (laps table too).
