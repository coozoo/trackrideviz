# Race Track Visualizer

[![Release_Version](https://github.com/coozoo/trackrideviz/actions/workflows/build_all.yml/badge.svg)](https://github.com/coozoo/trackrideviz/actions/workflows/build_all.yml)
[![Tests](https://github.com/coozoo/trackrideviz/actions/workflows/tests.yml/badge.svg)](https://github.com/coozoo/trackrideviz/actions/workflows/tests.yml)
[![Copr build status](https://copr.fedorainfracloud.org/coprs/yura/trackrideviz/package/trackrideviz/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/yura/trackrideviz/package/trackrideviz/)

Telemetry overlays for action camera video: speed, lap times, G-force, track map, heart rate,
power, OBD data and more, drawn as gauges on top of your footage and exported as a new video.

A cross-platform (Linux, Windows, macOS) Qt application meant to replace older telemetry overlay
editors that are no longer maintained and hardly run on current systems.

<p align="center">
  <a href="docs/images/mainview.png"><img src="docs/images/mainview.png" width="32%" alt="A video with gauges, the route on a street map and the data below"></a>
  <a href="docs/images/templates.png"><img src="docs/images/templates.png" width="32%" alt="A template applied to the video"></a>
  <a href="docs/images/export.png"><img src="docs/images/export.png" width="32%" alt="Exporting the video"></a>
</p>

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

## Support Ukraine

🇺🇦 Ukraine is fighting for its independence against an empire: russia's full-scale invasion, begun
in 2022. If this program is useful to you, please consider helping Ukraine's defenders through
[Come Back Alive](https://savelife.in.ua/en/donate-en/#donate-army-card-once), one of the
largest and most trusted foundations supporting them.

The program links to this section rather than to a fund directly: a link inside a binary can
be swapped for a fake one by whoever repackages it, while this page is public and its history
can be checked.

## Planned features

- Open a video and get its telemetry automatically (embedded GoPro data, or a `.fit` / `.gpx` next to it)
- Automatic sync by GPS time, manual fine-tuning against the video
- Data from FIT, GPX, TCX, NMEA, VBO and any CSV logger through data profiles
- Channels of several files in one project, such as a GPS plus a heart-rate strap, lined up by their clocks
- Derived channels: G-force from GPS, distance, grade, gear, smoothing, math
- Lap timing: start/finish line on the map, sectors, best / previous lap, delta to best
- Themed gauges that are not tied to one metric; templates; built-in gauge editor
- Import of your own gauges, templates and data profiles made with other programs
- Export through ffmpeg with software and hardware encoders, profiles (YouTube, small file, your own);
  the gauges alone on a transparent background (ProRes 4444 or VP9 WebM), to lay over the video in another editor
- Music under the video: a playlist of files, with fades and the volumes of the video and the music

The [user manual](docs/manual.md) tells how to use it, step by step.

## Installation

Packages for Linux (AppImage, .deb) and Windows are on the
[releases page](https://github.com/coozoo/trackrideviz/releases).

Fedora, from [Copr](https://copr.fedorainfracloud.org/coprs/yura/trackrideviz/):

```bash
sudo dnf copr enable yura/trackrideviz
sudo dnf install trackrideviz
```

Ubuntu, from the [PPA](https://launchpad.net/~coozoo/+archive/ubuntu/trackrideviz):

```bash
sudo add-apt-repository ppa:coozoo/trackrideviz
sudo apt install trackrideviz
```

Both are updated by the package manager with the rest of the system, and open projects, gauges
and templates with the program by a double click.

On Windows, take whichever you prefer:

- `..._WIN64_setup.exe` installs it, for you alone (no administrator needed) or for everyone, with
  a Start menu entry, and opens projects, gauges and templates with it by a double click. It is
  uninstalled from Windows' list of apps.
- `..._WIN64.zip` is the same program to unpack anywhere and run from there; nothing is installed.

On macOS the downloaded app has to be allowed to run first:

```bash
# adjust the location to where the app is
xattr -dr com.apple.quarantine /Applications/trackrideviz.app
codesign --force --deep --sign - /Applications/trackrideviz.app
```

Export needs ffmpeg, from [Homebrew](https://brew.sh) (`brew install ffmpeg`) or MacPorts; the program
finds it there by itself.

## Command line

The program exports without its window too, for scripts and for many videos in a row:

```bash
trackrideviz --render ride.rtvproj                       # as the project was saved
trackrideviz --render ride.rtvproj -o ride.mp4 --profile youtube --hardware
trackrideviz --render --video GX010042.MP4 --data ride.fit --template "Track day"
trackrideviz --render ride.rtvproj --gauges-only -o gauges.mov
trackrideviz --render --data ride.fit --background 1920x1080@30,#202830   # no camera video
trackrideviz --list-encoders                             # what this computer can encode with
trackrideviz --render ride.rtvproj --engine ffmpeg         # with the ffmpeg program, not the built-in engine
trackrideviz --help                                      # everything else
```

A project is exported as it was saved. Without one, the video and the data are put in step by
their clocks, as when they are opened in the window, or by `--offset`. The export profile and
the kind of encoder (graphics card or processor) are those the export dialog used last unless
`--profile`, `--encoder`, `--hardware` or `--software` say otherwise. Ctrl+C stops and keeps a
playable file. Exit codes: 0 done, 1 failed, 2 wrong arguments, 130 stopped.

## Build

Requires Qt 6.2.4 or newer (Core, Gui, Widgets, OpenGLWidgets, Multimedia, Concurrent, Svg, Linguist
tools), zlib and a C++17 compiler.
FFmpeg's libraries 6.1 or newer (libavformat, libavcodec, libavfilter, libavutil, libswresample, found
through pkg-config) are optional: with them the program has its own video engine; `CONFIG+=no_libav`
builds without them.
`ffmpeg` (with `ffprobe`) is needed at run time for export and for the music's lengths; the Windows
package comes with them. Otherwise the ones next to the program, then (on macOS) Homebrew's and MacPorts', then
on the PATH, are used, and their location can be set in the settings.

```bash
qmake6
make -j$(nproc)
./trackrideviz                                  # then File > Open video / Open data
./trackrideviz 20250715_074932A.mp4 ride.fit    # or name the files directly
```

**Windows build under Wine** (for trying the Windows build on Linux; Windows itself needs none of
this). Export has software encoders only there.

1. Color profiles. Qt 6.12 reads each screen's color profile with `WcsCreateIccProfile`, which
   Wine does not have: the program stops at start with "unimplemented function
   mscms.dll.WcsCreateIccProfile". Windows' own `mscms.dll` has it. Take the 64-bit one out of
   the Windows 7 SP1 update (winetricks keeps it in its cache once a verb has needed it; else
   download it from Microsoft, about 900 MB), put it beside `trackrideviz.exe`, and run the
   program with it:

   ```bash
   SP1=~/.cache/winetricks/win7sp1/windows6.1-KB976932-X64.exe
   [ -f "$SP1" ] || { mkdir -p "$(dirname "$SP1")" && wget -O "$SP1" http://download.windowsupdate.com/msdownload/update/software/svpk/2011/02/windows6.1-kb976932-x64_74865ef2562006e51d7f9333b4a8d45b7a749dab.exe; }
   echo "f4d1d418d91b1619688a482680ee032ffd2b65e420c6d2eaecf8aa3762aa64c8  $SP1" | sha256sum -c -
   cabextract -q -d /tmp/mscms -F "amd64_microsoft-windows-icm-base_31bf3856ad364e35_6.1.7601.17514_none_80edefc3a002c386/mscms.dll" "$SP1"
   cp /tmp/mscms/amd64_*/mscms.dll path/to/trackrideviz_WIN64/
   cd path/to/trackrideviz_WIN64 && WINEDLLOVERRIDES="mscms=n,b" wine trackrideviz.exe
   ```

   Without it: move `sRGB Color Space Profile.icm` out of
   `~/.wine/drive_c/windows/system32/spool/drivers/color/`; Qt then reads no profile and takes
   sRGB, the same to the eye.
2. Sound. Qt finds no sound device unless each output device has a container ID, which Wine
   does not store; add one per device:

   ```bash
   for k in $(grep -o 'MMDevices\\\\Audio\\\\Render\\\\{[^}]*}\\\\Properties' ~/.wine/system.reg | sed 's/\\\\\\\\/\\\\/g' | sort -u); do
     wine reg add "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\$k" /v "{8C7ED206-3F8A-4827-B3AB-AE9E1FAEFC6C},2" \
       /t REG_SZ /d "$(echo "$k" | grep -o '{[^}]*}')" /f
   done
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
debian/, *.spec, windows/, MAC_build_RELEASE.sh, .github/workflows/   packaging and CI
```

## Contributing

Patches are welcome. A few rules of this project that are easy to miss.

### Send pull requests to `slave`, not `main`

`slave` is the integration branch: work is collected there and `main` only ever receives merges
from it. Every push to `main` builds a release, so nothing lands there untested. Branch off
`slave`:

```bash
git remote add upstream https://github.com/coozoo/trackrideviz.git
git fetch upstream
git switch -c my-fix upstream/slave
```

If you already branched off `main`, rebase instead of only switching the base in the GitHub UI,
which would drag `main`'s merge commits into `slave`:

```bash
git rebase --onto upstream/slave upstream/main my-fix
git push --force-with-lease
```

Set the base to `slave` *before* you push. Changing the base afterwards fires a `pull_request`
event of type `edited`, which does not start the build; push once more to start it.

### Build tags

A push to `slave` or a pull request against it builds every platform, kept as workflow artifacts
rather than a release. Tags in the last commit message (or in the PR title or description) build
only some:

| Tag | Builds |
|---|---|
| `[build linux]` | Linux: the AppImage (the .deb is built too, but kept only in a release) |
| `[build win]` | Windows: the zip and the installer |
| `[build mac]` | the macOS .dmg (same as `[build mac dmg]`) |
| `[build mac zip]` | the macOS .zip |
| `[skip ci]` | nothing |

macOS is left out of the build matrix until the final stage; the tags for it do nothing until then.

### It has to build with Qt 6.2

The Linux packages are built on Ubuntu 22.04 with its own Qt 6.2, so that they run on every
system still in use and on all newer ones. Use nothing newer than Qt 6.2's API, and keep in
mind that Qt 6.2 plays video through GStreamer, Qt 6.4 and later through FFmpeg.

### One concern per pull request

Please don't bundle build-configuration changes with a fix. In particular leave the release
logging default alone:

```qmake
CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT
```

For `qDebug()` output while working on something, build in debug mode instead
(`qmake6 CONFIG+=debug`).

### Text and translations

Text in the program and the repository is in American English. New strings go through `tr()`;
update the `.ts` files with Qt 6's lupdate (see [Translations](#translations)). The Ukrainian
translation is kept complete: translate your strings, or say in the PR that you could not.

### Run the tests before submitting

CI does not run them, so please do (see [Build](#build)), and say in the PR on which platforms
you tried the change.

### Keep the PR description in sync

GitHub does not rewrite a pull request's description when you force-push. If you drop or change
part of your work, edit the description too.

### If you are contributing from a fork

- The first pull request of a new contributor waits for a maintainer to approve its workflow
  run. This is normal.
- Pull requests from forks get no repository secrets, so steps that publish can fail for reasons
  that have nothing to do with your patch.

## Author

[coozoo](https://github.com/coozoo)

## License

[GPL-3.0](LICENSE)

Gauges use the Barlow Semi Condensed font by Jeremy Tribby, under the SIL Open Font License
([text](src/core/gauge/fonts/OFL-BarlowSemiCondensed.txt)). Imported gauges are drawn with Roboto and
Roboto Condensed ([text](src/core/gauge/fonts/OFL-Roboto.txt)), Orbitron by Matt McInerney
([text](src/core/gauge/fonts/OFL-Orbitron.txt)) and DSEG7 Classic by keshikan
([text](src/core/gauge/fonts/OFL-DSEG.txt)) where the computer lacks the fonts they name, all under the
same license.

The icons that stand in where the desktop has none are from the Tango Desktop Project, in the
public domain ([text](images/icons/COPYING.Tango)).

Race Track Visualizer is an independent project, not affiliated with the makers of any other
program whose files it can read. No code or artwork from other products is included.
