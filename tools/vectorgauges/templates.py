#!/usr/bin/env python3
"""Writes the templates that ship with the program into data/templates.

A template is a layout of the gauges in data/gauges: which ones, showing
what, where on a 1920 x 1080 frame and in which colours. Each gauge hangs on
an edge of the frame (or its middle), so a template fits videos of another
shape too.

    python3 tools/vectorgauges/templates.py [data folder]

Run it after make.py (gauge sizes are read from data/gauges), look at the
result, commit data/. Gauges that overlap or leave the frame are reported.
"""

import json
import os
import shutil
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "data")
WIDTH, HEIGHT = 1920.0, 1080.0
MARGIN = 40

_shapes = {}


def shape(gauge):
    """Width of a gauge for a height of 1."""
    if gauge not in _shapes:
        with open(os.path.join(ROOT, "gauges", gauge, "gauge.json")) as f:
            w, h = json.load(f)["size"]
        _shapes[gauge] = w / h
    return _shapes[gauge]


def width(gauge, height):
    return height * shape(gauge)


def _offset(edge, margin, extent):
    if edge == "start":
        return margin
    if edge == "middle":
        return margin - extent / 2
    return -margin - extent


def place(gauge, channel, edge_x, edge_y, margin_x, margin_y, height):
    """One gauge `margin` away from its edges (from the middle for "middle": + right / down)."""
    return {"gauge": "bundled:" + gauge, "channel": channel, "edgeX": edge_x, "edgeY": edge_y,
            "offsetX": round(_offset(edge_x, margin_x, width(gauge, height)), 1),
            "offsetY": round(_offset(edge_y, margin_y, height), 1), "height": height}


def row(parts, edge_x, edge_y, margin_x, margin_y, height, gap=0):
    """Gauges side by side, left to right. `parts`: (gauge, channel)."""
    widths = [width(g, height) for g, _ in parts]
    total = sum(widths) + gap * (len(parts) - 1)
    # left side of the row, as an offset from its edge
    x = _offset(edge_x, margin_x, total)
    items = []
    for (gauge, channel), w in zip(parts, widths):
        items.append({"gauge": "bundled:" + gauge, "channel": channel, "edgeX": edge_x, "edgeY": edge_y,
                      "offsetX": round(x, 1), "offsetY": round(_offset(edge_y, margin_y, height), 1),
                      "height": height})
        x += w + gap
    return items


def column(parts, edge_x, edge_y, margin_x, margin_y, height, gap=0, step_x=0):
    """Gauges one under another, top to bottom; `step_x` shifts each one sideways from the one above."""
    total = height * len(parts) + gap * (len(parts) - 1)
    y = _offset(edge_y, margin_y, total)
    items = []
    for i, (gauge, channel) in enumerate(parts):
        items.append({"gauge": "bundled:" + gauge, "channel": channel, "edgeX": edge_x, "edgeY": edge_y,
                      "offsetX": round(_offset(edge_x, margin_x + step_x * i, width(gauge, height)), 1),
                      "offsetY": round(y, 1), "height": height})
        y += height + gap
    return items


def theme(background, accent, accent2, foreground="#ffffffff"):
    """Colours as #aarrggbb."""
    return {"background": background, "accent": accent, "accent2": accent2, "foreground": foreground}


def rect(item):
    edge = {"start": 0.0, "middle": 0.5, "end": 1.0}
    name = item["gauge"].split(":", 1)[1]
    x = edge[item["edgeX"]] * WIDTH + item["offsetX"]
    y = edge[item["edgeY"]] * HEIGHT + item["offsetY"]
    return x, y, x + width(name, item["height"]), y + item["height"]


def check(name, items):
    rects = [rect(i) for i in items]
    for i, a in enumerate(rects):
        if a[0] < -0.5 or a[1] < -0.5 or a[2] > WIDTH + 0.5 or a[3] > HEIGHT + 0.5:
            print(f"{name}: {items[i]['gauge']} leaves the frame {[round(v) for v in a]}")
        for j in range(i):
            b = rects[j]
            if min(a[2], b[2]) - max(a[0], b[0]) > 1 and min(a[3], b[3]) - max(a[1], b[1]) > 1:
                print(f"{name}: {items[i]['gauge']} overlaps {items[j]['gauge']}")


def write(name, category, colours, *groups):
    items = []
    for group in groups:
        items += group if isinstance(group, list) else [group]
    check(name, items)
    document = {"format": "rtvtemplate", "version": 1, "name": name, "category": category, "theme": colours,
                "items": items}
    with open(os.path.join(ROOT, "templates", name + ".json"), "w") as f:
        json.dump(document, f, indent=4, sort_keys=True)
        f.write("\n")


# --- colours ---------------------------------------------------------------

ORANGE = theme("#960a0e14", "#ffff781e", "#ffdce6f0")
RED = theme("#a0000000", "#ffe61e28", "#ffffffff")
GREEN = theme("#9614281e", "#ff78dc5a", "#ffe6f0dc")
PLAIN = theme("#6e000000", "#ffffffff", "#ffc8c8c8")
CYAN = theme("#a00a1418", "#ff1ed2e6", "#ffe6f0f0")
BLUE = theme("#a0081420", "#ff2a9df4", "#ffdce6f0")
YELLOW = theme("#a0101010", "#ffffc81e", "#fff0f0e6")
PINK = theme("#a0140a14", "#ffff4678", "#fff0e6f0")
SEA = theme("#a0061a26", "#ff3cc8b4", "#ffdcf0f0")


def main():
    folder = os.path.join(ROOT, "templates")
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    M = MARGIN

    # --- cycling -----------------------------------------------------------
    write("Ride", "Cycling", ORANGE,
          place("Arc", "speed", "start", "end", M, M, 300),
          place("Icon readout", "heartRate", "start", "end", 360, 240, 100),
          place("Bar", "cadence", "start", "end", 360, M, 100),
          place("Profile", "elevation", "end", "end", M, M, 170),
          place("Route map", "speed", "end", "start", M, M, 300))
    # everything about the ride in one round gauge
    write("Ride cluster", "Cycling", CYAN,
          place("Ride cluster", "speed", "start", "end", M, M, 380),
          place("Progress", "distance", "middle", "end", 0, M, 90),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    # a strip along the bottom
    write("Ride strip", "Cycling", ORANGE,
          row([("Heading tape", "heading"), ("Tile grade", "grade"), ("Tile elevation", "elevation"),
               ("Tile", "distance"), ("Tile", "heartRate"), ("Tile cadence", "cadence"), ("Tile speed", "speed")],
              "middle", "end", 0, 0, 64),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    # a column down the right side
    write("Ride tiles", "Cycling", BLUE,
          place("Route map arrow", "speed", "end", "end", 0, 150 + 6 * 72, 236),
          column([("Tile speed", "speed"), ("Tile", "heartRate"), ("Tile cadence", "cadence"),
                  ("Tile elevation", "elevation"), ("Tile grade", "grade"), ("Tile", "distance")],
                 "end", "end", 0, 150, 72))
    # the same tiles as steps
    write("Steps", "Cycling", CYAN,
          column([("Tile speed", "speed"), ("Tile", "heartRate"), ("Tile cadence", "cadence"),
                  ("Tile elevation", "elevation"), ("Tile grade", "grade"), ("Tile", "distance")],
                 "start", "end", M, M, 70, gap=4, step_x=30),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    write("Heart and cadence", "Cycling", BLUE,
          column([("Heart", "heartRate"), ("Dial small", "cadence"), ("Dial small", "speed")],
                 "start", "end", M, M, 160, gap=10),
          place("Climb wedge", "elevation", "end", "end", M, M, 180),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    write("Speed and cadence", "Cycling", YELLOW,
          place("Dial pair", "speed", "start", "end", M, M, 380),
          place("Crank", "cadence", "start", "end", M + 400, M, 170),
          place("Heart pulse", "heartRate", "start", "end", M + 590, M, 170),
          place("Graph", "elevation", "end", "end", M, M, 170),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    write("Twin dials", "Cycling", RED,
          place("Half dial left", "speed", "start", "end", M, M, 340),
          place("Half dial right", "power", "end", "end", M, M, 340),
          place("Odometer", "distance", "middle", "end", 0, M + 110, 56),
          place("Progress", "distance", "middle", "end", 0, M, 90),
          place("Route map arrow", "speed", "end", "start", M, M, 260))
    write("Columns", "Cycling", GREEN,
          place("Dial ring", "speed", "start", "end", M, M, 340),
          row([("Column glass", "power"), ("Column glass", "heartRate"), ("Column glass", "cadence")],
              "end", "end", M, M, 300, gap=12),
          place("Route map arrow", "speed", "end", "start", M, M, 260))
    # power
    write("Power", "Cycling", PINK,
          place("Ride cluster power", "speed", "start", "end", M, M, 400),
          place("Graph pair", "power", "end", "end", M, M, 190),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    write("Pedalling", "Cycling", PINK,
          row([("Power phase left", "leftPowerPhaseStart"), ("Power phase right", "rightPowerPhaseStart")],
              "start", "start", M, M, 200, gap=10),
          row([("Pedal offset left", "leftPlatformCenterOffset"), ("Pedal offset right", "rightPlatformCenterOffset")],
              "start", "start", M + 35, M + 215, 190, gap=72),
          row([("Label plate", "cadence"), ("Label plate", "heartRate")], "start", "start", M, M + 420, 76, gap=12),
          place("Dial fill", "power", "end", "end", M, M, 320),
          place("Tile speed", "speed", "end", "end", M + 340, M, 80),
          place("Route map arrow", "speed", "end", "start", M, M, 280))
    write("Mountain pass", "Cycling", GREEN,
          place("Side arc left", "grade", "start", "middle", M, 0, 440),
          place("Tape elevation", "elevation", "end", "middle", M, 0, 600),
          place("Climb wedge", "elevation", "middle", "end", 0, M, 190),
          row([("Tile speed", "speed"), ("Tile", "heartRate")], "start", "start", M, M, 76, gap=8))

    # --- simple ------------------------------------------------------------
    write("Minimal", "Simple", PLAIN,
          place("Icon readout", "speed", "start", "end", M, M, 90),
          place("Icon readout", "heartRate", "start", "end", M, 140, 90))
    write("Speed", "Simple", PLAIN,
          place("Dial", "speed", "start", "end", M, M + 66, 340),
          place("Odometer", "distance", "start", "end", M + (340 - width("Odometer", 54)) / 2, M, 54))
    write("Pills", "Simple", PLAIN,
          row([("Pill readout", "speed"), ("Pill readout", "heartRate"), ("Pill readout", "elevation"),
               ("Pill readout", "distance")], "middle", "end", 0, M, 56, gap=20))
    write("Slants", "Simple", ORANGE,
          column([("Slant readout", "speed"), ("Slant readout", "heartRate"), ("Slant readout", "cadence")],
                 "start", "end", M, M, 92, gap=10))
    write("Rings", "Simple", CYAN,
          row([("Ring readout", "speed"), ("Segment ring", "heartRate"), ("Circle readout", "grade")],
              "start", "end", M, M, 210, gap=16),
          place("Compass ring", "heading", "end", "end", M, M, 210))
    write("Arcs", "Simple", YELLOW,
          place("Open arc", "speed", "start", "end", M, M, 340),
          place("Open arc thin", "heartRate", "start", "end", M + 350, M, 280),
          place("Crown arc", "grade", "end", "end", M, M, 150))
    # the whole recording as two lines, with the present moment marked
    write("Lines", "Simple", BLUE,
          place("Graph window", "speed", "start", "end", M, M, 160),
          place("Graph window", "elevation", "end", "end", M, M, 160),
          place("Pill readout", "distance", "middle", "start", 0, M, 56))

    # --- motorsport --------------------------------------------------------
    write("Track day", "Motorsport", RED,
          place("Arc", "speed", "start", "end", M, M, 340),
          place("Lap map", "speed", "end", "start", M, M, 340),
          place("G-force", "gForceLateral", "end", "end", M, M, 260),
          place("Bar", "speed", "middle", "start", 0, M, 90))
    laps = [("Readout right", "lap"), ("Lap time right", "lapTime"), ("Lap time right", "previousLapTime"),
            ("Lap time right", "bestLapTime")]
    write("Lap timer", "Motorsport", RED,
          place("Dial metal arc", "speed", "start", "end", M, M, 360),
          place("G-force rings", "gForceLateral", "start", "end", M + 380, M, 230),
          column(laps, "end", "end", M, M, 70, gap=6),
          place("Lap map arrow", "speed", "end", "start", M, M, 320))
    write("Race", "Motorsport", YELLOW,
          place("Crown arc centred", "gForceLateral", "middle", "start", 0, M, 150),
          place("Side arc centred", "gForceLongitudinal", "start", "middle", M, 0, 440),
          place("Dome", "speed", "middle", "end", 0, M, 230),
          column(laps, "end", "end", M, M, 70, gap=6),
          place("Lap map arrow", "speed", "end", "start", M, M, 300))
    write("G-force", "Motorsport", CYAN,
          place("Dial glass", "speed", "start", "end", M, M, 340),
          place("Accel bar", "gForceLongitudinal", "middle", "end", 0, M, 76),
          place("Skid ball", "gForceLateral", "middle", "end", 0, M + 90, 100),
          place("G-force rings", "gForceLateral", "end", "end", M, M, 300),
          place("Dial g", "gForceVertical", "end", "end", M + 320, M, 180))
    write("Dashboard", "Motorsport", ORANGE,
          place("Dial notched", "speed", "middle", "end", 0, M, 340),
          place("Dial small metal", "heartRate", "middle", "end", -290, M, 200),
          place("Dial g", "gForceLongitudinal", "middle", "end", 290, M, 200),
          column(laps, "end", "start", M, M + 340, 64, gap=6),
          place("Lap map arrow", "speed", "end", "start", M, M, 320))

    # --- outdoor -----------------------------------------------------------
    write("Climb", "Outdoor", GREEN,
          place("Profile", "elevation", "middle", "end", 0, M, 200),
          place("Column", "grade", "start", "end", M, M, 330),
          place("Icon readout", "speed", "start", "start", M, M, 100),
          place("Icon readout", "heartRate", "start", "start", M, 160, 100),
          place("Route map", "speed", "end", "start", M, M, 280))
    write("Hike", "Outdoor", GREEN,
          place("Compass rose", "heading", "start", "start", M, M, 230),
          place("Climb wedge", "elevation", "start", "end", M, M, 200),
          column([("Tile elevation", "elevation"), ("Tile", "distance"), ("Tile speed", "speed"),
                  ("Tile", "heartRate")], "end", "end", M, M, 74, gap=6),
          place("Route map arrow", "speed", "end", "start", M, M, 300))
    write("Descent", "Outdoor", BLUE,
          place("Dome", "speed", "start", "end", M, M, 240),
          place("Side arc right", "elevation", "end", "middle", M, 0, 440),
          place("Thermometer", "temperature", "end", "end", M + 200, M, 260),
          place("Graph", "elevation", "middle", "end", 0, M, 170),
          place("Compass ring", "heading", "start", "start", M, M, 200))
    write("Field boxes", "Outdoor", SEA,
          place("Compass ring", "heading", "start", "end", M, M, 260),
          place("Half round", "speed", "start", "end", M + 280, M, 200),
          column([("Field box", "heading"), ("Field box", "speed"), ("Field box", "distance"),
                  ("Field box", "temperature")], "end", "end", M, M, 92, gap=8),
          place("Route map arrow", "speed", "end", "start", M, M, 300))
    # instruments: heading above, speed and height at the sides, the slope in the middle
    write("Instruments", "Outdoor", CYAN,
          place("Heading tape", "heading", "middle", "start", 0, 20, 80),
          place("Column scale", "speed", "start", "middle", M, 0, 440),
          place("Tape elevation", "elevation", "end", "middle", M, 0, 580),
          place("Horizon", "gradeAngle", "middle", "middle", 0, 0, 300),
          place("Skid ball", "gForceLateral", "middle", "middle", 0, 220, 80),
          row([("Tile speed", "speed"), ("Tile", "verticalSpeed"), ("Tile", "gForceVertical"),
               ("Tile heading", "heading"), ("Tile elevation", "elevation")], "middle", "end", 0, 0, 62, gap=6))

    print(f"{len(os.listdir(folder))} templates written to {folder}")


if __name__ == "__main__":
    main()
