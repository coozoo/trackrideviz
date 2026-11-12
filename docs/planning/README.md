# Race Track Visualizer — planning

Working documents. They describe where the project is going; the code says where it is.

Status: **M0 done** (repo skeleton, 2026-11-12). Next: M1, first overlay on screen.

| # | File | What it holds |
|---|------|---------------|
| 2 | [02-formats.md](02-formats.md) | Telemetry, gauge, template and project formats: what we import and how |
| 3 | [03-architecture.md](03-architecture.md) | Modules, threading, video pipeline, native gauge model |
| 4 | [04-roadmap.md](04-roadmap.md) | Milestones M0…M8 |
| 5 | [05-decisions.md](05-decisions.md) | Decisions taken and still open |
| 6 | [06-fit-notes.md](06-fit-notes.md) | FIT decoder checklist and prior-art review tasks |

## Workspace layout

```
claudecode/trackrideviz/
├── reference/       reference material. Not in git, never shipped
├── reference-notes/ notes on other programs. Not in git
└── trackrideviz/    this repo
```

## Summary

A UI-independent core (`src/core`: telemetry model, importers, lap engine, gauge model, QPainter
gauge renderer, video export) with a Qt Widgets desktop app on top, and room for a simplified QML
front end later that reuses the same core. Gauges are drawn by one QPainter code path used for
preview, export and thumbnails alike. The native gauge format is JSON, modelled as a superset of
`.veg` components plus inputs/calculators; `.veg` gauges, `.vet` templates and other gauge and
data-profile formats are *imported* from the user's own copies. Video export goes through an
`ExportBackend` interface: an external `ffmpeg` process fed overlay frames over a pipe first,
in-process libav* later if needed.
