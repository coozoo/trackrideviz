# Race Track Visualizer

Telemetry overlays for action camera video: speed, lap times, G-force, track map, heart rate,
power, OBD data and more, drawn as gauges on top of your footage and exported as a new video.

A cross-platform (Linux, Windows, macOS) Qt application meant to replace older overlay editors, another editor
and another editor, which are no longer maintained and hardly run on current systems.

> **Status: early development.** The repository currently holds the application skeleton and the
> plan. Nothing useful can be rendered yet. See the [roadmap](docs/planning/04-roadmap.md).

## Planned features

- Open a video and get its telemetry automatically (embedded GoPro data, or a `.fit` / `.gpx` next to it)
- Automatic sync by GPS time, manual fine-tuning against the video
- Data from FIT, GPX, TCX, NMEA, VBO and any CSV logger through data profiles
- Derived channels: G-force from GPS, distance, grade, gear, smoothing, math
- Lap timing: start/finish line on the map, sectors, best / previous lap, delta to best
- Themed gauges that are not tied to one metric; templates; built-in gauge editor
- Import of your own .veg gauges and .vet templates and another editor gauges and data profiles
- Export through ffmpeg with software and hardware encoders; overlay-only export with alpha

## Build

Requires Qt 6.2.4 or newer (Core, Gui, Widgets, Linguist tools) and a C++17 compiler.
`ffmpeg` will be needed at run time for export.

```bash
qmake6
make -j$(nproc)
./trackrideviz
```

Tests:

```bash
mkdir -p build/debug && cd build/debug
qmake6 ../../trackrideviz.pro CONFIG+=debug CONFIG+=tests
make -j$(nproc)
make check
```

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

Race Track Visualizer is an independent project. It is not affiliated with other programs or
another editor; those names belong to their owners and are mentioned only to describe file-format
compatibility. No code or artwork from those products is included.
