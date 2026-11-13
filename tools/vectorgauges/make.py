#!/usr/bin/env python3
"""Writes the drawn (SVG) gauges that ship with the program into data/gauges.

Every gauge is a gauge.json and the drawings it names in images/. Drawings
use the theme's colours through @foreground, @background, @accent, @accent2
(and @<name>-opacity for their transparency), so they follow the theme the
user picks. Scale numbers, values and units are texts of the gauge, not part
of a drawing: they follow the channel and the units.

    python3 tools/vectorgauges/make.py [data folder]

Run it after changing a design here, look at the result, commit data/.
"""

import json
import math
import os
import shutil
import sys

FONT = "Barlow Semi Condensed"
ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "data")
GAUGES = os.path.join(ROOT, "gauges")

ROUND_TYPES = ["speed", "cadence", "power", "heartRate", "engineRpm", "engineRpmX1000"]


# --- drawing helpers -------------------------------------------------------

def svg(width, height, body, defs=""):
    return ("<svg xmlns='http://www.w3.org/2000/svg' version='1.2' baseProfile='tiny' "
            f"viewBox='0 0 {width} {height}' width='{width}' height='{height}'>"
            + (f"<defs>{defs}</defs>" if defs else "") + body + "</svg>\n")


def fmt(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def polar(cx, cy, r, angle):
    """Angle in degrees clockwise from 12 o'clock."""
    a = math.radians(angle)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def theme(name, attr="fill"):
    """A theme colour with its transparency."""
    return f"{attr}='@{name}' {attr}-opacity='@{name}-opacity'"


def ring_path(cx, cy, r_outer, r_inner, start, end):
    """A ring segment from `start` to `end` degrees (clockwise from 12 o'clock)."""
    large = 1 if (end - start) % 360 > 180 else 0
    x0, y0 = polar(cx, cy, r_outer, start)
    x1, y1 = polar(cx, cy, r_outer, end)
    x2, y2 = polar(cx, cy, r_inner, end)
    x3, y3 = polar(cx, cy, r_inner, start)
    return (f"M{fmt(x0)},{fmt(y0)} A{fmt(r_outer)},{fmt(r_outer)} 0 {large} 1 {fmt(x1)},{fmt(y1)} "
            f"L{fmt(x2)},{fmt(y2)} A{fmt(r_inner)},{fmt(r_inner)} 0 {large} 0 {fmt(x3)},{fmt(y3)} Z")


def ticks(cx, cy, start, end, count, r_outer, length, width, colour="accent2", every=1, skip_every=0):
    """`count` ticks evenly from start to end; with skip_every, every n-th is left out."""
    lines = []
    for i in range(count):
        if skip_every and i % skip_every == 0:
            continue
        if i % every:
            continue
        angle = start + (end - start) * i / (count - 1)
        x0, y0 = polar(cx, cy, r_outer, angle)
        x1, y1 = polar(cx, cy, r_outer - length, angle)
        lines.append(f"M{fmt(x0)},{fmt(y0)} L{fmt(x1)},{fmt(y1)}")
    return (f"<path d='{' '.join(lines)}' fill='none' stroke='@{colour}' stroke-opacity='@{colour}-opacity' "
            f"stroke-width='{fmt(width)}' stroke-linecap='butt'/>")


def metal_bezel(cx, cy, r_outer, r_inner, gid="metal"):
    """A brushed ring, lit from the top left. Neutral greys: metal is metal in every theme."""
    defs = (f"<linearGradient id='{gid}' x1='0' y1='0' x2='1' y2='1'>"
            "<stop offset='0' stop-color='#f2f2f2'/><stop offset='0.35' stop-color='#9a9a9a'/>"
            "<stop offset='0.6' stop-color='#4a4a4a'/><stop offset='1' stop-color='#c8c8c8'/></linearGradient>"
            f"<linearGradient id='{gid}In' x1='1' y1='1' x2='0' y2='0'>"
            "<stop offset='0' stop-color='#e0e0e0'/><stop offset='0.5' stop-color='#555555'/>"
            "<stop offset='1' stop-color='#2a2a2a'/></linearGradient>")
    mid = (r_outer + r_inner) / 2
    body = (f"<circle cx='{cx}' cy='{cy}' r='{fmt(mid)}' fill='none' stroke='url(#{gid})' "
            f"stroke-width='{fmt(r_outer - r_inner)}'/>"
            f"<circle cx='{cx}' cy='{cy}' r='{fmt(r_inner + 1.5)}' fill='none' stroke='url(#{gid}In)' stroke-width='3'/>")
    return defs, body


def plate(cx, cy, r, glow=True, gid="plate"):
    """The dial's face in the theme's background, a little lighter in the middle."""
    defs = (f"<radialGradient id='{gid}' cx='0.5' cy='0.42' r='0.6'>"
            "<stop offset='0' stop-color='#ffffff' stop-opacity='0.10'/>"
            "<stop offset='1' stop-color='#ffffff' stop-opacity='0'/></radialGradient>")
    body = f"<circle cx='{cx}' cy='{cy}' r='{fmt(r)}' {theme('background')}/>"
    if glow:
        body += f"<circle cx='{cx}' cy='{cy}' r='{fmt(r)}' fill='url(#{gid})'/>"
    return defs, body


def gloss(cx, cy, r, gid="gloss"):
    """A glass reflection over the upper half."""
    defs = (f"<linearGradient id='{gid}' x1='0' y1='0' x2='0' y2='1'>"
            "<stop offset='0' stop-color='#ffffff' stop-opacity='0.28'/>"
            "<stop offset='1' stop-color='#ffffff' stop-opacity='0'/></linearGradient>")
    rx, ry = r * 0.86, r * 0.55
    body = f"<ellipse cx='{cx}' cy='{fmt(cy - r * 0.42)}' rx='{fmt(rx)}' ry='{fmt(ry)}' fill='url(#{gid})'/>"
    return defs, body


def needle(cx, cy, length, tail, width, colour="accent", cap=0.0, cap_colour="background"):
    """Pointing to 12 o'clock, turning about (cx, cy)."""
    tip = cy - length
    body = (f"<path d='M{fmt(cx - width / 2)},{fmt(cy + tail)} L{fmt(cx - 1.2)},{fmt(tip)} "
            f"L{fmt(cx + 1.2)},{fmt(tip)} L{fmt(cx + width / 2)},{fmt(cy + tail)} Z' {theme(colour)}/>")
    if cap:
        body += (f"<circle cx='{cx}' cy='{cy}' r='{fmt(cap)}' fill='#1c1c1c'/>"
                 f"<circle cx='{cx}' cy='{cy}' r='{fmt(cap)}' fill='none' stroke='#8a8a8a' stroke-width='2'/>"
                 f"<circle cx='{fmt(cx - cap * 0.25)}' cy='{fmt(cy - cap * 0.3)}' r='{fmt(cap * 0.35)}' "
                 "fill='#ffffff' fill-opacity='0.18'/>")
    return body


# --- gauge helpers ----------------------------------------------------------

def image(file, frame, colour="original", manipulations=None):
    c = {"type": "image", "image": f"images/{file}", "frame": list(frame), "color": colour}
    if manipulations:
        c["manipulations"] = manipulations
    return c


def text(auto, at, size, colour="foreground", align="center", valign="center", bold=False, italic=False, **extra):
    font = {"family": FONT, "size": size}
    if bold:
        font["bold"] = True
    if italic:
        font["italic"] = True
    c = {"type": "text", "autoText": auto, "frame": list(at), "font": font, "color": colour,
         "align": align, "valign": valign}
    c.update(extra)
    return c


def rotate(start, end, pivot, **extra):
    m = {"type": "rotate", "minAngle": start, "maxAngle": end, "pivot": list(pivot)}
    m.update(extra)
    return m


def radial_clip(start, end, pivot):
    return {"type": "radialClip", "minAngle": start, "maxAngle": end, "pivot": list(pivot)}


def markings(cx, cy, r, start, end, count, size, colour="foreground", italic=False):
    """Scale numbers: the n-th gets the n-th value of the channel's range."""
    out = []
    for i in range(count):
        angle = start + (end - start) * i / (count - 1)
        x, y = polar(cx, cy, r, angle)
        out.append(text("intervalMarking", (round(x, 1), round(y, 1)), size, colour, italic=italic, precision=0))
    return out


def write(name, size, components, drawings, types):
    folder = os.path.join(GAUGES, name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(os.path.join(folder, "images"))
    for file, content in drawings.items():
        with open(os.path.join(folder, "images", file), "w", encoding="utf-8") as f:
            f.write(content)
    gauge = {"format": "rtvgauge", "version": 1, "name": name, "size": list(size),
             "supportedTypes": types, "components": components}
    with open(os.path.join(folder, "gauge.json"), "w", encoding="utf-8") as f:
        json.dump(gauge, f, indent=4)
        f.write("\n")
    print(name)


# --- the gauges -------------------------------------------------------------

C = 200          # centre of a 400 x 400 round gauge
SWEEP = (-135, 135)


def outer_needle(colour="accent"):
    """Only the outer part of a needle, from the edge of the inner disc to the scale."""
    x0, x1 = 4.5, 1.2
    return (f"<path d='M{C - x0},{C - 84} L{C - x1},{C - 164} L{C + x1},{C - 164} L{C + x0},{C - 84} Z' "
            f"{theme(colour)}/>")


def pointer():
    """A bright line at the scale and an arrowhead at the inner disc, both turning together."""
    return (f"<path d='M{C - 1.6},{C - 166} L{C + 1.6},{C - 166} L{C + 1.6},{C - 128} L{C - 1.6},{C - 128} Z' "
            f"{theme('accent2')}/>"
            f"<path d='M{C},{C - 104} L{C - 11},{C - 82} L{C + 11},{C - 82} Z' {theme('accent')}/>")


def round_dial(name, *, bezel="flat", sweep=None, face="plain", gloss_on=False, needle_style="slim",
               labels=7, label_radius=118, label_size=24, majors=13, value_y=240, value_size=64,
               ring_only=False, unit_caps=False, second_readout=False):
    defs, body = plate(C, C, 168)
    drawings = {}
    components = []

    if bezel == "metal":
        d, b = metal_bezel(C, C, 196, 170)
        defs += d
        body = f"<circle cx='{C}' cy='{C}' r='196' fill='#202020'/>" + body + b
    elif bezel == "flat":
        body += f"<circle cx='{C}' cy='{C}' r='168' fill='none' {theme('accent2', 'stroke')} stroke-width='3'/>"
    elif bezel == "ring":
        body = (f"<circle cx='{C}' cy='{C}' r='182' fill='none' {theme('accent2', 'stroke')} stroke-width='2'/>"
                + body)

    if face == "sport":
        defs += ("<radialGradient id='sport' cx='0.5' cy='0.85' r='0.75'>"
                 "<stop offset='0' stop-color='@accent' stop-opacity='0.75'/>"
                 "<stop offset='0.55' stop-color='@accent' stop-opacity='0.25'/>"
                 "<stop offset='1' stop-color='@accent' stop-opacity='0'/></radialGradient>")
        body += f"<circle cx='{C}' cy='{C}' r='166' fill='url(#sport)'/>"

    if not ring_only:
        # an inner disc behind the readout
        body += (f"<circle cx='{C}' cy='{C}' r='80' fill='#000000' fill-opacity='0.3'/>"
                 f"<circle cx='{C}' cy='{C}' r='80' fill='none' stroke='#ffffff' stroke-opacity='0.08' stroke-width='2'/>")
    drawings["face.svg"] = svg(400, 400, body, defs)
    components.append(image("face.svg", (0, 0, 400, 400)))

    # what has been reached so far, swept from the start of the scale up to the needle
    if sweep in ("silver", "silver+accent"):
        band = ("<defs><linearGradient id='silver' x1='0' y1='0' x2='1' y2='1'>"
                "<stop offset='0' stop-color='#ffffff' stop-opacity='0.85'/>"
                "<stop offset='0.6' stop-color='#bdbdbd' stop-opacity='0.75'/>"
                "<stop offset='1' stop-color='#8c8c8c' stop-opacity='0.7'/></linearGradient></defs>"
                f"<path d='{ring_path(C, C, 162, 84, *SWEEP)}' fill='url(#silver)'/>")
        drawings["sweep.svg"] = svg(400, 400, band)
        components.append(image("sweep.svg", (0, 0, 400, 400), manipulations=[radial_clip(*SWEEP, (C, C))]))
    if sweep in ("accent", "silver+accent"):
        track = f"<path d='{ring_path(C, C, 140, 106, *SWEEP)}' fill='#ffffff' fill-opacity='0.06'/>"
        drawings["track.svg"] = svg(400, 400, track)
        components.append(image("track.svg", (0, 0, 400, 400)))
        drawings["band.svg"] = svg(400, 400, f"<path d='{ring_path(C, C, 140, 106, *SWEEP)}' {theme('accent')}/>")
        components.append(image("band.svg", (0, 0, 400, 400), manipulations=[radial_clip(*SWEEP, (C, C))]))

    minors = (majors - 1) * 5 + 1
    scale = ticks(C, C, *SWEEP, majors, 162, 22, 4)
    if not ring_only:
        scale += ticks(C, C, *SWEEP, minors, 162, 10, 1.6, skip_every=5)
    drawings["scale.svg"] = svg(400, 400, scale)
    components.append(image("scale.svg", (0, 0, 400, 400)))
    components += markings(C, C, label_radius, *SWEEP, labels, label_size, italic=True)

    if gloss_on:
        d, b = gloss(C, C, 166)
        drawings["glass.svg"] = svg(400, 400, b, d)

    if needle_style == "outer":
        drawn = outer_needle()
    elif needle_style == "pointer":
        drawn = pointer()
    elif needle_style == "slim":
        drawn = needle(C, C, 160, 26, 7, cap=16)
    elif needle_style == "bold":
        drawn = needle(C, C, 150, 34, 14, cap=24)
    else:   # "light": a bright needle for a coloured face
        drawn = needle(C, C, 160, 22, 6, colour="accent2", cap=14)
    drawings["needle.svg"] = svg(400, 400, drawn)
    if unit_caps:
        components.append(text("value", (C, C), value_size, bold=True, italic=True, precision=0))
        components.append(text("unit", (C, 336), 26, bold=True, upperCase=True))
    else:
        components.append(text("value", (C, value_y), value_size, bold=True, italic=True, precision=0))
        components.append(text("unit", (C, value_y + value_size * 0.62), 24, "accent2", italic=True))
    if second_readout:
        # the same value more precisely, small, under the unit
        components.append(text("valueAndUnit", (C, value_y + value_size * 1.05), 20, italic=True, precision=1))
    components.append(image("needle.svg", (0, 0, 400, 400), manipulations=[rotate(*SWEEP, (C, C))]))
    if gloss_on:
        components.append(image("glass.svg", (0, 0, 400, 400)))
    write(name, (400, 400), components, drawings, ROUND_TYPES)


def small_dial(name, *, metal=False):
    """A 200 x 200 dial: fewer numbers, a big value."""
    c = 100
    defs, body = plate(c, c, 86)
    if metal:
        d, b = metal_bezel(c, c, 98, 86)
        defs += d
        body = f"<circle cx='{c}' cy='{c}' r='98' fill='#202020'/>" + body + b
    else:
        # a broken outer ring
        body += (f"<circle cx='{c}' cy='{c}' r='94' fill='none' {theme('background', 'stroke')} stroke-width='8'/>"
                 + ticks(c, c, 0, 345, 24, 98, 8, 3, "foreground"))
    drawings = {"face.svg": svg(200, 200, body, defs)}
    scale = ticks(c, c, *SWEEP, 9, 84, 11, 3) + ticks(c, c, *SWEEP, 33, 84, 5, 1.2, skip_every=4)
    drawings["scale.svg"] = svg(200, 200, scale)
    drawings["needle.svg"] = svg(200, 200, needle(c, c, 80, 12, 5, cap=8))
    components = [image("face.svg", (0, 0, 200, 200)), image("scale.svg", (0, 0, 200, 200))]
    components += markings(c, c, 58, *SWEEP, 5, 13)
    components += [text("value", (c, 128), 40, bold=True, italic=True, precision=0),
                   text("unit", (c, 158), 15, "accent2", italic=True),
                   image("needle.svg", (0, 0, 200, 200), manipulations=[rotate(*SWEEP, (c, c))])]
    write(name, (200, 200), components, drawings, ROUND_TYPES)


def open_arc(name):
    """A thick three-quarter arc from 6 o'clock round to 3, the value in the open corner."""
    start, end = -180, 90
    track = f"<path d='{ring_path(C, C, 180, 130, start, end)}' {theme('background')}/>"
    bar = f"<path d='{ring_path(C, C, 176, 134, start, end)}' {theme('accent')}/>"
    drawings = {"track.svg": svg(400, 400, track), "fill.svg": svg(400, 400, bar)}
    components = [image("track.svg", (0, 0, 400, 400)),
                  image("fill.svg", (0, 0, 400, 400), manipulations=[radial_clip(start, end, (C, C))]),
                  text("value", (300, 316), 84, bold=True, italic=True, precision=0),
                  text("unit", (300, 368), 28, "accent2", italic=True)]
    write(name, (400, 400), components, drawings, ROUND_TYPES)


def compass(name):
    """A rose that turns with the heading under a fixed mark at the top."""
    defs, body = plate(C, C, 190, gid="plate")
    body += f"<circle cx='{C}' cy='{C}' r='190' fill='none' {theme('accent2', 'stroke')} stroke-width='2'/>"
    drawings = {"face.svg": svg(400, 400, body, defs)}
    rose = ticks(C, C, 0, 360, 73, 184, 10, 1.6, "foreground") + ticks(C, C, 0, 360, 13, 184, 20, 3.5)
    letters = [("N", 0, "accent", 40), ("E", 90, "foreground", 34), ("S", 180, "foreground", 34),
               ("W", 270, "foreground", 34), ("NE", 45, "foreground", 22), ("SE", 135, "foreground", 22),
               ("SW", 225, "foreground", 22), ("NW", 315, "foreground", 22)]
    for label, angle, colour, size in letters:
        x, y = polar(C, C, 138, angle)
        rose += (f"<text x='{fmt(x)}' y='{fmt(y + size * 0.36)}' font-family='{FONT}' font-size='{size}' "
                 f"font-weight='bold' text-anchor='middle' transform='rotate({angle} {fmt(x)} {fmt(y)})' "
                 f"{theme(colour)}>{label}</text>")
    drawings["rose.svg"] = svg(400, 400, rose)
    mark = f"<path d='M{C},34 L{C - 12},8 L{C + 12},8 Z' {theme('accent')}/>"
    drawings["mark.svg"] = svg(400, 400, mark)
    components = [image("face.svg", (0, 0, 400, 400)),
                  image("rose.svg", (0, 0, 400, 400), manipulations=[
                      rotate(0, -360, (C, C), minValue=0, maxValue=360, wrap=True)]),
                  image("mark.svg", (0, 0, 400, 400)),
                  text("value", (C, C), 64, bold=True, precision=0),
                  text("unit", (C, C + 44), 22, "accent2")]
    write(name, (400, 400), components, drawings, ["heading"])


HEART = ("M200,330 C150,290 70,235 70,160 C70,110 105,78 148,78 C172,78 190,92 200,110 "
         "C210,92 228,78 252,78 C295,78 330,110 330,160 C330,235 250,290 200,330 Z")


def heart(name):
    body = (f"<path d='{HEART}' {theme('background')}/>"
            f"<path d='{HEART}' fill='none' {theme('accent', 'stroke')} stroke-width='9' stroke-linejoin='round'/>")
    drawings = {"heart.svg": svg(400, 400, body)}
    components = [image("heart.svg", (0, 0, 400, 400)),
                  text("value", (200, 190), 84, bold=True, precision=0),
                  text("unit", (200, 250), 26, "accent2"),
                  text("label", (200, 372), 26, italic=True)]
    write(name, (400, 400), components, drawings, ["heartRate"])


def tile(name):
    """Icon, label and value on a plate with an accent edge; the icon follows the channel."""
    body = (f"<rect x='0' y='0' width='360' height='110' rx='6' {theme('background')}/>"
            f"<rect x='0' y='0' width='5' height='110' {theme('accent')}/>")
    drawings = {"plate.svg": svg(360, 110, body)}
    components = [image("plate.svg", (0, 0, 360, 110)),
                  {"type": "image", "channelIcon": True, "color": "accent", "frame": [22, 23, 64, 64]},
                  text("label", (104, 34), 22, "accent2", align="left", upperCase=True),
                  text("valueAndUnit", (104, 76), 42, align="left", bold=True, upperCase=True)]
    write(name, (360, 110), components, drawings,
          ["speed", "heartRate", "elevation", "cadence", "power", "temperature", "distance", "heading", "grade"])


def main():
    os.makedirs(GAUGES, exist_ok=True)
    # numbers every major tick, the needle only outside the inner disc
    round_dial("Dial", needle_style="outer", labels=13, label_radius=124, label_size=19, value_y=196)
    round_dial("Dial metal", bezel="metal", sweep="silver", needle_style="outer", labels=13, label_radius=124,
               label_size=19, value_y=196)
    round_dial("Dial metal arc", bezel="metal", sweep="silver+accent", needle_style="pointer", labels=13,
               label_radius=124, label_size=19, value_y=186, second_readout=True)
    round_dial("Dial ring", bezel="ring", ring_only=True, labels=7, needle_style="outer", unit_caps=True,
               value_size=72)
    round_dial("Dial sport", bezel="flat", face="sport", needle_style="light")
    round_dial("Dial glass", bezel="metal", gloss_on=True, needle_style="bold", labels=13, label_radius=124,
               value_y=286, value_size=48)
    small_dial("Dial small")
    small_dial("Dial small metal", metal=True)
    open_arc("Open arc")
    compass("Compass rose")
    heart("Heart")
    tile("Tile")


if __name__ == "__main__":
    main()
