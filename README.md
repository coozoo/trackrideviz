# Race Track Visualizer

[![Release_Version](https://github.com/coozoo/trackrideviz/actions/workflows/build_all.yml/badge.svg)](https://github.com/coozoo/trackrideviz/actions/workflows/build_all.yml)

Telemetry overlays for action camera video: speed, lap times, G-force, track map, heart rate,
power, OBD data and more, drawn as gauges on top of your footage and exported as a new video.

A cross-platform (Linux, Windows, macOS) Qt application meant to replace older telemetry overlay
editors that are no longer maintained and hardly run on current systems.

## Why

Once upon a time I wanted to put the data of a ride on its video, and found there was no good
program to do that easily on Linux - nor, really, on Windows. Most of what exists is outdated and
no longer supported: old .NET applications that crash often or are painfully slow, and on Linux
they simply do not work. So I decided to make such a program myself, with what AI makes possible
today.

> **Status: early development.** A video can be opened with a FIT, GPX or TCX file (or the data a GoPro wrote into the video itself), synchronized
> automatically, dressed with gauges that are placed and sized with the mouse, and exported as a
> new video. Gauges and templates made elsewhere (`.veg`, `.vet`, XML gauges and templates) can be imported,
> including graphs, track maps, dials and tapes; a first set of own gauges and templates ships with it. Channels a
> recording lacks (grade, G-force, heading...) are worked out from GPS; laps and sectors are timed from lines placed
> on the route, and a timeline shows the laps over the whole recording. Videos can be joined, parts cut out
> (stops found from the data), the data viewed and corrected. Our own gauges are drawn in SVG and can be
> passed on as `.rtvgauge` / `.rtvtemplate` files. Routes can be drawn on a street map (OpenStreetMap and
> others), whole or close up around the rider, and a missing or poor elevation taken from a terrain
> model. See the [roadmap](docs/planning/04-roadmap.md).

## Planned features

- Open a video and get its telemetry automatically (embedded GoPro data, or a `.fit` / `.gpx` next to it)
- Automatic sync by GPS time, manual fine-tuning against the video
- Data from FIT, GPX, TCX, NMEA, VBO and any CSV logger through data profiles
- Derived channels: G-force from GPS, distance, grade, gear, smoothing, math
- Lap timing: start/finish line on the map, sectors, best / previous lap, delta to best
- Themed gauges that are not tied to one metric; templates; built-in gauge editor
- Import of your own gauges, templates and data profiles made with other programs
- Export through ffmpeg with software and hardware encoders, profiles (YouTube, small file, your own);
  the gauges alone on a transparent background (ProRes 4444 or VP9 WebM), to lay over the video in another editor
- Music under the video: a playlist of files, with fades and the volumes of the video and the music

## Build

Requires Qt 6.2.4 or newer (Core, Gui, Widgets, OpenGLWidgets, Multimedia, Concurrent, Svg, Linguist
tools), zlib and a C++17 compiler.
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
binary at build time. To pick up new strings, with Qt 6's lupdate (`lupdate-qt6` on Fedora; a
plain `lupdate` may be an older Qt's, which rewrites the files in its own format):

```bash
lupdate-qt6 trackrideviz.pro
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

Gauges use the Barlow Semi Condensed font by Jeremy Tribby, under the SIL Open Font License
([text](src/core/gauge/fonts/OFL-BarlowSemiCondensed.txt)).

Race Track Visualizer is an independent project, not affiliated with the makers of any other
program whose files it can read. No code or artwork from other products is included.
