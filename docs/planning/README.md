# Race Track Visualizer — planning

Working documents. They describe where the project is going; the code says where it is.

Status: **M0 done** (repo skeleton, 2026-11-12). Next: M1, first overlay on screen.

| # | File | What it holds |
|---|------|---------------|
| 1 | [01-reference-apps.md](01-reference-apps.md) | What the old editor, another editor and another editor do; what we take from each |
| 2 | [02-formats.md](02-formats.md) | Telemetry, gauge, template and project formats: what we import and how |
| 3 | [03-architecture.md](03-architecture.md) | Modules, threading, video pipeline, native gauge model |
| 4 | [04-roadmap.md](04-roadmap.md) | Milestones M0…M8 |
| 5 | [05-decisions.md](05-decisions.md) | Decisions taken and still open |
| 6 | [06-fit-notes.md](06-fit-notes.md) | FIT decoder checklist and prior-art review tasks |

## Workspace layout

```
claudecode/trackrideviz/
├── reference/       archives + installers of the reference apps. Not in git, never shipped
└── trackrideviz/    this repo
```

## Summary

A UI-independent core (`src/core`: telemetry model, importers, lap engine, gauge model, QPainter
gauge renderer, video export) with a Qt Widgets desktop app on top, and room for a simplified QML
front end later that reuses the same core. Gauges are drawn by one QPainter code path used for
preview, export and thumbnails alike. The native gauge format is JSON, modelled as a superset of
the .veg format's `.veg` components plus another editor's inputs/calculators; the old editor gauges/templates and another editor
gauges/data profiles are *imported* from the user's own copies. Video export goes through an
`ExportBackend` interface: an external `ffmpeg` process fed overlay frames over a pipe first,
in-process libav* later if needed.
