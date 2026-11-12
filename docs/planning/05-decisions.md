# 5. Decisions

Status: ✅ agreed · 🟡 proposed, awaiting confirmation · ❓ open

## §1 ✅ Reference material stays out of the repo

The reference archives and installers are proprietary software plus the vendors' artwork and
music. Abandoned is not the same as free. They live in `../reference/` (outside this repo) and are
used to understand behaviour and file formats only:

- no decompiled code is copied or translated line by line into our sources;
- none of the vendors' PNGs, templates, themes, fonts or music are committed or shipped;
- the old editor/another editor import reads from the *user's own* install folder at run time
  ("import everything from this folder" in one go, or single files the user found elsewhere).

Open follow-up (manual review by Yuriy): read the licence texts in the installers to see whether any
content may be redistributed after all.

## §2 ✅ Export engine: external `ffmpeg` first

External `ffmpeg` process fed overlay frames over a pipe, behind an `ExportBackend` interface.
In-process libav* can be added later behind the same interface (needed for phones).

## §3 ✅ Licence: GPL-3.0

Not a from-scratch codebase and many dependencies; GPL keeps every FFmpeg configuration usable
(including a linked x264/x265 if backend B arrives) and is compatible with Qt's LGPL/GPL.

## §4 🟡 Preview via Qt Multimedia

`QMediaPlayer` + `QVideoSink` for preview; own decoder only if frame-exact seeking proves
inadequate for the sync UI. Note: Ubuntu 22.04's Qt 6.2 uses the GStreamer backend, newer Qt the
FFmpeg one - behaviour must be checked on both.

## §5 ✅ FIT: own decoder, after studying prior art

Write our own decoder, but first collect the pitfalls others already hit - see
[06-fit-notes.md](06-fit-notes.md).

## §6 ✅ Native formats are JSON; foreign formats are import-only

`.rtvgauge`, `.rtvtemplate`, `.rtvproj`, data profiles - all versioned JSON. the old editor `.veg`/`.vet` and
another editor gauge/profile XML are converted on import. another editor formats are not supported (binary).

## §7 ✅ Build and structure: qmake, same shape as the other apps

qmake, C++17, Qt ≥ 6.2.4, repo = `trackrideviz/trackrideviz`, `main.cpp` and `trackrideviz.pro` in
the root, version in `const QString APP_VERSION`. Reason: the release workflow, debian packaging,
RPM spec and macOS script of qtjsondiff/qtrfpowermeter all key on exactly that, and they are known
to work. CMake was considered and dropped - its only gain here would be tidier library targets.

## §8 ❓ Maps

Track map gauge needs no tiles (just the polyline). The *Map & Laps* view wants a real map under the
track. Overseer already carries MapGraphics (QGraphicsView-based, widgets, OSM tiles) - reuse it
rather than QtLocation/QML. Decide at M4.

## §9 ❓ GitHub side

Assumed `github.com/coozoo/trackrideviz` (used in the spec, debian/control, README). Launchpad PPA
upload is wired but disabled until the repository variable `LAUNCHPAD_PPA` is set.
