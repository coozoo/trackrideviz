# Race Track Visualizer

Telemetry overlays for action camera video: speed, lap times, G-force, track map, heart rate,
power, OBD data and more, drawn as gauges on top of your footage and exported as a new video.

A cross-platform (Linux, Windows, macOS) Qt application meant to replace older telemetry overlay
editors that are no longer maintained and hardly run on current systems.

> **Status: early development.** A video can be opened with a FIT or GPX file, synchronized
> automatically, dressed with gauges that are placed and sized with the mouse, and exported as a
> new video. Gauges and templates made elsewhere (`.veg`, `.vet`, XML gauges and templates) can be imported,
> including graphs, track maps, dials and tapes; a first set of own gauges and templates ships with it. Channels a
> recording lacks (grade, G-force, heading...) are worked out from GPS; laps and sectors are timed from lines placed
> on the route, and a timeline shows the laps over the whole recording. See the [roadmap](docs/planning/04-roadmap.md).

## Planned features

- Open a video and get its telemetry automatically (embedded GoPro data, or a `.fit` / `.gpx` next to it)
- Automatic sync by GPS time, manual fine-tuning against the video
- Data from FIT, GPX, TCX, NMEA, VBO and any CSV logger through data profiles
- Derived channels: G-force from GPS, distance, grade, gear, smoothing, math
- Lap timing: start/finish line on the map, sectors, best / previous lap, delta to best
- Themed gauges that are not tied to one metric; templates; built-in gauge editor
- Import of your own gauges, templates and data profiles made with other programs
- Export through ffmpeg with software and hardware encoders; overlay-only export with alpha

## Build

Requires Qt 6.2.4 or newer (Core, Gui, Widgets, OpenGLWidgets, Multimedia, Concurrent, Svg, Linguist
tools) and a C++17 compiler.
`ffmpeg` (with `ffprobe`) is needed at run time for export; its location can be set in the settings.

```bash
qmake6
make -j$(nproc)
./trackrideviz                                  # then File > Open video / Open data
./trackrideviz 20250715_074932A.mp4 ride.fit    # or name the files directly
```

Tests:

```bash
mkdir -p build/debug && cd build/debug
qmake6 ../../trackrideviz.pro CONFIG+=debug CONFIG+=tests
make -j$(nproc)
make check
```

Export tests that stop a running export take about a minute together and are left out unless
`RTV_SLOW_TESTS=1` is set.

The importers can also be run over your own recordings (nothing is written, a summary is printed):

```bash
RTV_FIT_SAMPLES=/folder/with/fit/files ./tests-shadow/fit/tst_fit sampleFiles
RTV_GPX_SAMPLE=/path/to/track.gpx      ./tests-shadow/gpx/tst_gpx sampleFile
```

## Translations

English and Ukrainian. Sources are in `translations/`, compiled `.qm` files are embedded into the
binary at build time. To pick up new strings:

```bash
lupdate trackrideviz.pro
```

## Project layout

```
main.cpp, trackrideviz.pro   application entry and project file
src/core/                    UI-independent part: telemetry, gauges, export (QtCore + QtGui only)
src/                         Qt Widgets user interface
tests/                       unit tests (QtTest)
translations/                .ts files
docs/planning/               design documents and roadmap
debian/, *.spec, MAC_build_RELEASE.sh, .github/workflows/   packaging and CI
```

## License

[GPL-3.0](LICENSE)

Race Track Visualizer is an independent project, not affiliated with the makers of any other
program whose files it can read. No code or artwork from other products is included.
