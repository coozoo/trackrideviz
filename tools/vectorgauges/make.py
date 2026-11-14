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


def ticks(cx, cy, start, end, count, r_outer, length, width, colour="accent2", every=1, skip_every=0, opacity=None):
    """`count` ticks evenly from start to end; with skip_every, every n-th is left out.
    `opacity`: their own transparency instead of the theme colour's."""
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
    alpha = f"@{colour}-opacity" if opacity is None else fmt(opacity)
    return (f"<path d='{' '.join(lines)}' fill='none' stroke='@{colour}' stroke-opacity='{alpha}' "
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


def thin_metal_bezel(cx, cy, r_outer, r_inner, gid="metal"):
    """A thin brushed rim on the inner third; outside it a shade, mostly see-through."""
    silver = r_inner + (r_outer - r_inner) / 3
    defs, body = metal_bezel(cx, cy, silver, r_inner, gid)
    mid = (silver + r_outer) / 2
    shade = (f"<circle cx='{cx}' cy='{cy}' r='{fmt(mid)}' fill='none' stroke='#000000' stroke-opacity='0.2' "
             f"stroke-width='{fmt(r_outer - silver)}'/>")
    return defs, shade + body


def plate(cx, cy, r, glow=True, gid="plate", opacity=None):
    """The dial's face in the theme's background, a little lighter in the middle.
    `opacity`: its own instead of the theme colour's."""
    defs = (f"<radialGradient id='{gid}' cx='0.5' cy='0.42' r='0.6'>"
            "<stop offset='0' stop-color='#ffffff' stop-opacity='0.10'/>"
            "<stop offset='1' stop-color='#ffffff' stop-opacity='0'/></radialGradient>")
    fill = theme('background') if opacity is None else f"fill='@background' fill-opacity='{fmt(opacity)}'"
    body = f"<circle cx='{cx}' cy='{cy}' r='{fmt(r)}' {fill}/>"
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


def outer_needle(colour="accent", tip=164, base=84, inverted=False):
    """Only the outer part of a needle, from about the edge of the inner disc to the scale.
    Inverted: thin at the root, widening outwards."""
    x0, x1 = (1.2, 5.0) if inverted else (4.5, 1.2)
    return (f"<path d='M{C - x0},{C - base} L{C - x1},{C - tip} L{C + x1},{C - tip} L{C + x0},{C - base} Z' "
            f"{theme(colour)}/>")


def pointer(tip=168, base=75):
    """A white needle, thin at the root and widening to the rim, with an accent arrowhead riding on
    its inner part: only a sliver of white shows between the inner disc and the arrowhead."""
    white = outer_needle("accent2", tip=tip, base=base, inverted=True)
    root, head = 82, 116
    return white + f"<path d='M{C},{C - head} L{C - 15},{C - root} L{C + 15},{C - root} Z' {theme('accent')}/>"


def glow_needle(tail=38):
    """Wide at the middle, narrowing to the tip, light fading into the accent, with a straight strip
    for a tail past the middle; a soft glow for a hub and an accent glow round the needle, fading
    towards the tip."""
    def shape(grow):
        # the needle and its tail widened by `grow` on every side
        return (f"M{fmt(C - 7 - grow)},{fmt(C + tail + grow)} L{fmt(C - 7 - grow)},{C} "
                f"L{fmt(C - 1.2 - grow)},{fmt(C - 162 - grow)} L{fmt(C + 1.2 + grow)},{fmt(C - 162 - grow)} "
                f"L{fmt(C + 7 + grow)},{C} L{fmt(C + 7 + grow)},{fmt(C + tail + grow)} Z")
    # SVG Tiny has no blur: a few ever wider, ever fainter copies make the glow
    glow = "".join(f"<path d='{shape(g)}' fill='url(#needleGlow)' fill-opacity='{o}'/>"
                   for g, o in ((9, 0.35), (6, 0.5), (3.5, 0.7), (1.5, 1.0)))
    return ("<defs><linearGradient id='glowNeedle' x1='0' y1='1' x2='0' y2='0'>"
            "<stop offset='0' stop-color='#ffffff' stop-opacity='0.95'/>"
            "<stop offset='0.35' stop-color='#ffffff' stop-opacity='0.95'/>"
            "<stop offset='1' stop-color='@accent' stop-opacity='@accent-opacity'/></linearGradient>"
            "<linearGradient id='needleGlow' x1='0' y1='1' x2='0' y2='0'>"
            "<stop offset='0' stop-color='@accent' stop-opacity='0.3'/>"
            "<stop offset='1' stop-color='@accent' stop-opacity='0.04'/></linearGradient>"
            "<radialGradient id='hub' cx='0.5' cy='0.5' r='0.5'>"
            "<stop offset='0' stop-color='@accent' stop-opacity='0.55'/>"
            "<stop offset='1' stop-color='@accent' stop-opacity='0'/></radialGradient></defs>"
            f"<circle cx='{C}' cy='{C}' r='34' fill='url(#hub)'/>"
            + glow + f"<path d='{shape(0)}' fill='url(#glowNeedle)'/>")


def taper_needle(foot=12, tip=3):
    """A thick taper with a rounded foot at the middle, no pin."""
    return (f"<path d='M{C - foot},{C} L{C - tip},{C - 150} L{C + tip},{C - 150} L{C + foot},{C} "
            f"A{foot},{foot} 0 0 1 {C - foot},{C} Z' {theme('accent')}/>")


def half_gloss():
    """Glass over the upper half, its lower edge straight through the middle."""
    return ("<defs><linearGradient id='halfGloss' x1='0' y1='0' x2='0' y2='1'>"
            "<stop offset='0' stop-color='#ffffff' stop-opacity='0.22'/>"
            "<stop offset='1' stop-color='#ffffff' stop-opacity='0.10'/></linearGradient></defs>"
            f"<path d='M{C - 166},{C} A166,166 0 0 1 {C + 166},{C} Z' fill='url(#halfGloss)'/>")


def round_dial(name, *, bezel="flat", sweep=None, face="plain", gloss_on=False, needle_style="slim",
               labels=7, label_radius=118, label_size=24, label_italic=True, majors=13, value_y=240, value_size=64,
               minor_ticks=True, inner_disc=True, tick_outer=162, major_length=22, minor_length=10,
               minor_width=1.6, minor_opacity=None, needle_base=84, needle_tip=None, needle_inverted=False, unit_caps=False, second_readout=False, unit_gap=0.62):
    defs, body = plate(C, C, 168)
    drawings = {}
    components = []

    if bezel == "metal":
        d, b = metal_bezel(C, C, 196, 170)
        defs += d
        body = f"<circle cx='{C}' cy='{C}' r='196' fill='#202020'/>" + body + b
    elif bezel == "shade":
        # a black ring round the face, and outside it a dark band, darker at the top
        defs += ("<linearGradient id='shade' x1='0' y1='0' x2='0' y2='1'>"
                 "<stop offset='0' stop-color='#000000' stop-opacity='0.5'/>"
                 "<stop offset='1' stop-color='#000000' stop-opacity='0.08'/></linearGradient>")
        body += (f"<circle cx='{C}' cy='{C}' r='172.5' fill='none' stroke='#000000' stroke-width='9'/>"
                 f"<path d='{ring_path(C, C, 186.5, 177, 0, 359.99)}' fill='url(#shade)'/>")
    elif bezel == "thin metal":
        d, b = thin_metal_bezel(C, C, 196, 168)
        defs += d
        body += b
    elif bezel == "flat":
        body += f"<circle cx='{C}' cy='{C}' r='168' fill='none' {theme('accent2', 'stroke')} stroke-width='3'/>"
    elif bezel == "rim":
        # a thin brushed silver ring just outside the face
        d, b = metal_bezel(C, C, 172.5, 168, gid="rim")
        defs += d
        # partly see-through under the face: with the theme's background about 80 % opaque in all
        body = f"<circle cx='{C}' cy='{C}' r='172.5' fill='#141414' fill-opacity='0.55'/>" + body + b
    elif bezel == "ring":
        body = (f"<circle cx='{C}' cy='{C}' r='182' fill='none' {theme('accent2', 'stroke')} stroke-width='2'/>"
                + body)

    if face == "sport":
        defs += ("<radialGradient id='sport' cx='0.5' cy='0.85' r='0.75'>"
                 "<stop offset='0' stop-color='@accent' stop-opacity='0.75'/>"
                 "<stop offset='0.55' stop-color='@accent' stop-opacity='0.25'/>"
                 "<stop offset='1' stop-color='@accent' stop-opacity='0'/></radialGradient>")
        body += f"<circle cx='{C}' cy='{C}' r='166' fill='url(#sport)'/>"

    if inner_disc:
        # an inner disc behind the readout
        body += (f"<circle cx='{C}' cy='{C}' r='80' fill='#000000' fill-opacity='0.3'/>"
                 f"<circle cx='{C}' cy='{C}' r='80' fill='none' stroke='#ffffff' stroke-opacity='0.08' stroke-width='2'/>")
    drawings["face.svg"] = svg(400, 400, body, defs)
    components.append(image("face.svg", (0, 0, 400, 400)))

    # what has been reached so far, swept from the start of the scale up to the needle
    if sweep in ("silver", "silver+accent"):
        band = ("<defs><linearGradient id='silver' x1='0' y1='0' x2='1' y2='1'>"
                "<stop offset='0' stop-color='#ffffff' stop-opacity='0.6'/>"
                "<stop offset='0.6' stop-color='#bdbdbd' stop-opacity='0.5'/>"
                "<stop offset='1' stop-color='#8c8c8c' stop-opacity='0.45'/></linearGradient></defs>"
                f"<path d='{ring_path(C, C, max(162, tick_outer - 1), 84, *SWEEP)}' fill='url(#silver)'/>")
        drawings["sweep.svg"] = svg(400, 400, band)
        components.append(image("sweep.svg", (0, 0, 400, 400), manipulations=[radial_clip(*SWEEP, (C, C))]))
    if sweep in ("accent", "silver+accent"):
        track = f"<path d='{ring_path(C, C, 140, 106, *SWEEP)}' fill='#ffffff' fill-opacity='0.06'/>"
        drawings["track.svg"] = svg(400, 400, track)
        components.append(image("track.svg", (0, 0, 400, 400)))
        drawings["band.svg"] = svg(400, 400, f"<path d='{ring_path(C, C, 140, 106, *SWEEP)}' {theme('accent')}/>")
        components.append(image("band.svg", (0, 0, 400, 400), manipulations=[radial_clip(*SWEEP, (C, C))]))

    minors = (majors - 1) * 5 + 1
    scale = ticks(C, C, *SWEEP, majors, tick_outer, major_length, 4)
    if minor_ticks:
        scale += ticks(C, C, *SWEEP, minors, tick_outer, minor_length, minor_width, skip_every=5,
                       opacity=minor_opacity)
    drawings["scale.svg"] = svg(400, 400, scale)
    components.append(image("scale.svg", (0, 0, 400, 400)))
    components += markings(C, C, label_radius, *SWEEP, labels, label_size, italic=label_italic)

    if gloss_on == "half":
        drawings["glass.svg"] = svg(400, 400, half_gloss())
    elif gloss_on:
        d, b = gloss(C, C, 166)
        drawings["glass.svg"] = svg(400, 400, b, d)

    if needle_style == "outer":
        drawn = outer_needle(tip=needle_tip or max(164, tick_outer - 2), base=needle_base, inverted=needle_inverted)
    elif needle_style == "pointer":
        drawn = pointer(tip=needle_tip or 168, base=needle_base)
    elif needle_style == "glow":
        drawn = glow_needle()
    elif needle_style == "taper":
        drawn = taper_needle()
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
        components.append(text("unit", (C, value_y + value_size * unit_gap), 24, "accent2", italic=True))
    if second_readout:
        # the same value more precisely, small, under the unit
        components.append(text("valueAndUnit", (C, value_y + value_size * 1.05), 20, italic=True, precision=1))
    components.append(image("needle.svg", (0, 0, 400, 400), manipulations=[rotate(*SWEEP, (C, C))]))
    if gloss_on:
        components.append(image("glass.svg", (0, 0, 400, 400)))
    write(name, (400, 400), components, drawings, ROUND_TYPES)


def segmented_dial(name):
    """A 200 x 200 dial: a ring of light segments, four numbers round it, a short needle."""
    c = 100
    defs, body = plate(c, c, 98)
    # light and dark in about equal parts
    segments = []
    count = 20
    for i in range(count):
        start = i * 360 / count - 360 / count * 0.26
        segments.append(f"<path d='{ring_path(c, c, 98, 88, start, start + 360 / count * 0.52)}' "
                        "fill='#d8d8d8' fill-opacity='0.85'/>")
    body += (f"<circle cx='{c}' cy='{c}' r='93' fill='none' stroke='#000000' stroke-opacity='0.35' stroke-width='10'/>"
             + "".join(segments)
             + f"<circle cx='{c}' cy='{c}' r='52' fill='#ffffff' fill-opacity='0.03'/>")
    drawings = {"face.svg": svg(200, 200, body, defs)}
    # a taper with a rounded foot, reaching two thirds of the way out
    drawings["needle.svg"] = svg(200, 200, f"<path d='M{c - 5.5},{c} L{c - 1},{c - 66} L{c + 1},{c - 66} "
                                           f"L{c + 5.5},{c} A5.5,5.5 0 0 1 {c - 5.5},{c} Z' {theme('accent')}/>")
    components = [image("face.svg", (0, 0, 200, 200))]
    # bottom, left, top, right: the needle turns from the first to the last
    components += [text("intervalMarking", at, 12, align=align, precision=0)
                   for at, align in (((100, 182), "center"), ((16, 100), "left"), ((100, 18), "center"),
                                     ((184, 100), "right"))]
    components += [text("value", (c, 130), 45, italic=True, precision=0),
                   text("unit", (c, 160), 22, italic=True),
                   image("needle.svg", (0, 0, 200, 200), manipulations=[rotate(-180, 90, (c, c))])]
    write(name, (200, 200), components, drawings, ["cadence", "heartRate", "speed", "power"])


def knob(cx, cy, r):
    """A round knob lit from the top left, its edge in shadow."""
    return (f"<defs><radialGradient id='knob' cx='0.35' cy='0.3' r='0.75'>"
            "<stop offset='0' stop-color='#6a6a6a'/><stop offset='0.55' stop-color='#2a2a2a'/>"
            "<stop offset='1' stop-color='#111111'/></radialGradient></defs>"
            f"<circle cx='{cx}' cy='{cy}' r='{fmt(r + 2)}' fill='#000000' fill-opacity='0.35'/>"
            f"<circle cx='{cx}' cy='{cy}' r='{fmt(r)}' fill='url(#knob)'/>"
            f"<circle cx='{cx}' cy='{cy}' r='{fmt(r - 0.6)}' fill='none' stroke='#000000' stroke-width='1.2'/>")


def small_metal_dial(name):
    """A 200 x 200 dial: a thin silver rim with a shade outside it, a see-through face, a strip of a
    needle on a big knob, the unit above it and the value below."""
    c = 100
    defs, body = plate(c, c, 86, opacity=0.4)
    d, b = thin_metal_bezel(c, c, 98, 86)
    defs += d
    body += b
    drawings = {"face.svg": svg(200, 200, body, defs)}
    # thin ticks a little longer than the main ones, both up to the silver
    scale = (ticks(c, c, *SWEEP, 9, 85, 7, 2.6)
             + ticks(c, c, *SWEEP, 33, 85, 10, 1, skip_every=4, opacity=0.5))
    drawings["scale.svg"] = svg(200, 200, scale)
    strip = f"<path d='M{c - 2.2},{c + 10} L{c - 2.2},{c - 80} L{c + 2.2},{c - 80} L{c + 2.2},{c + 10} Z' {theme('accent')}/>"
    drawings["needle.svg"] = svg(200, 200, strip)
    # its own drawing, so the light on it stays where it is while the needle turns
    drawings["knob.svg"] = svg(200, 200, knob(c, c, 24))
    components = [image("face.svg", (0, 0, 200, 200)), image("scale.svg", (0, 0, 200, 200))]
    components += markings(c, c, 62, *SWEEP, 5, 13)
    components += [text("unit", (c, 56), 15, "accent2", italic=True),
                   text("value", (c, 150), 40, bold=True, italic=True, precision=0),
                   image("needle.svg", (0, 0, 200, 200), manipulations=[rotate(*SWEEP, (c, c))]),
                   image("knob.svg", (0, 0, 200, 200))]
    write(name, (200, 200), components, drawings, ROUND_TYPES)


def open_arc(name):
    """A thick three-quarter arc from 6 o'clock round to 3, the value in the open corner."""
    start, end = -180, 90
    # only the indicator, no track behind it
    bar = f"<path d='{ring_path(C, C, 176, 134, start, end)}' {theme('accent')}/>"
    drawings = {"fill.svg": svg(400, 400, bar)}
    components = [image("fill.svg", (0, 0, 400, 400), manipulations=[radial_clip(start, end, (C, C))]),
                  text("value", (300, 316), 84, bold=True, italic=True, precision=0),
                  text("unit", (300, 368), 28, "accent2", italic=True)]
    write(name, (400, 400), components, drawings, ROUND_TYPES)


def compass(name):
    """A rose that turns with the heading under a fixed mark at the top."""
    # mostly see-through: a compass sits over the picture for the whole video
    defs, body = plate(C, C, 190, gid="plate", opacity=0.3)
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
                  text("valueAndUnit", (C, C), 64, bold=True, precision=0)]
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


TILE_ICON = (22, 23, 64, 64)       # where the icon sits on a tile
IX, IY = 54, 55                    # its middle


def stroke(colour, width, opacity=None):
    """`opacity`: its own transparency instead of the theme colour's."""
    alpha = f"@{colour}-opacity" if opacity is None else fmt(opacity)
    return f"fill='none' stroke='@{colour}' stroke-opacity='{alpha}' stroke-width='{fmt(width)}'"


def live_icon(kind):
    """The icon of a tile that shows the value itself: (drawings, components)."""
    full = (0, 0, 360, 110)
    if kind == "speed":
        # a small speedometer: an open dial and a needle that turns
        dial = (f"<path d='{ring_path(IX, IY, 30, 25, -135, 135)}' {theme('accent')}/>"
                + ticks(IX, IY, -135, 135, 7, 23, 5, 2.4, "accent"))
        needle_ = (f"<path d='M{IX - 2.4},{IY} L{IX - 0.8},{IY - 22} L{IX + 0.8},{IY - 22} L{IX + 2.4},{IY} Z' "
                   f"{theme('accent')}/><circle cx='{IX}' cy='{IY}' r='4.5' {theme('accent')}/>")
        return ({"icon.svg": svg(360, 110, dial), "needle.svg": svg(360, 110, needle_)},
                [image("icon.svg", full), image("needle.svg", full, manipulations=[rotate(-135, 135, (IX, IY))])])
    if kind == "elevation":
        # a mountain that fills up as the road climbs
        mountain = f"M{IX - 30},{IY + 26} L{IX - 8},{IY - 22} L{IX + 3},{IY - 4} L{IX + 12},{IY - 16} L{IX + 30},{IY + 26} Z"
        outline = f"<path d='{mountain}' {stroke('accent', 3)} stroke-linejoin='round'/>"
        fill = f"<path d='{mountain}' {theme('accent')}/>"
        clip = {"type": "heightClip", "minY": IY + 26, "maxY": IY - 22}
        return ({"icon.svg": svg(360, 110, outline), "fill.svg": svg(360, 110, fill)},
                [image("icon.svg", full), image("fill.svg", full, manipulations=[clip])])
    if kind == "heading":
        # one big arrowhead with a notch at its back, pointing the way
        ring = f"<circle cx='{IX}' cy='{IY}' r='29' {stroke('accent', 3)}/>"
        needle_ = (f"<path d='M{IX},{IY - 23} L{IX + 15},{IY + 17} L{IX},{IY + 8} L{IX - 15},{IY + 17} Z' "
                   f"{theme('accent')} stroke-linejoin='round'/>")
        turn = rotate(0, 360, (IX, IY), minValue=0, maxValue=360, wrap=True)
        return ({"icon.svg": svg(360, 110, ring), "needle.svg": svg(360, 110, needle_)},
                [image("icon.svg", full), image("needle.svg", full, manipulations=[turn])])
    if kind == "grade":
        # a slope that fills from the left as it gets steeper
        slope = f"M{IX - 30},{IY + 22} L{IX + 30},{IY + 22} L{IX + 30},{IY - 22} Z"
        outline = f"<path d='{slope}' {stroke('accent', 3)} stroke-linejoin='round'/>"
        fill = f"<path d='{slope}' {theme('accent')}/>"
        clip = {"type": "widthClip", "minX": IX - 30, "maxX": IX + 30}
        return ({"icon.svg": svg(360, 110, outline), "fill.svg": svg(360, 110, fill)},
                [image("icon.svg", full), image("fill.svg", full, manipulations=[clip])])
    if kind == "arc":
        # an arc that fills round: cadence, engine speed, power
        track = f"<path d='{ring_path(IX, IY, 30, 22, -135, 135)}' fill='@accent' fill-opacity='0.25'/>"
        fill = (f"<path d='{ring_path(IX, IY, 30, 22, -135, 135)}' {theme('accent')}/>")
        dot = f"<circle cx='{IX}' cy='{IY}' r='5' {theme('accent')}/>"
        return ({"icon.svg": svg(360, 110, track + dot), "fill.svg": svg(360, 110, fill)},
                [image("icon.svg", full), image("fill.svg", full, manipulations=[radial_clip(-135, 135, (IX, IY))])])
    raise ValueError(kind)


def tile(name, kind=None, types=None):
    """Icon, label and value on a plate with an accent edge. Without `kind` the icon is a picture of
    whatever channel the tile shows; with it, the icon moves with the value."""
    # mostly see-through, so a row of tiles hides little of the video
    body = (f"<rect x='0' y='0' width='360' height='110' rx='6' fill='@background' fill-opacity='0.35'/>"
            f"<rect x='0' y='0' width='5' height='110' {theme('accent')}/>")
    drawings = {"plate.svg": svg(360, 110, body)}
    components = [image("plate.svg", (0, 0, 360, 110))]
    if kind:
        icon_drawings, icon_components = live_icon(kind)
        drawings.update(icon_drawings)
        components += icon_components
    else:
        components.append({"type": "image", "channelIcon": True, "color": "accent", "frame": list(TILE_ICON)})
    components += [text("label", (104, 34), 22, "accent2", align="left", upperCase=True),
                   text("valueAndUnit", (104, 76), 42, align="left", bold=True, upperCase=True, precision=0)]
    write(name, (360, 110), components, drawings, types or
          ["speed", "heartRate", "elevation", "cadence", "power", "temperature", "distance", "heading", "grade"])


# --- batch 2: rides, graphs, maps ------------------------------------------

def group(components, manipulations=None):
    g = {"type": "group", "components": components}
    if manipulations:
        g["manipulations"] = manipulations
    return g


def translate(min_x=0, max_x=0, min_y=0, max_y=0, **extra):
    m = {"type": "translate", "minX": min_x, "maxX": max_x, "minY": min_y, "maxY": max_y}
    m.update(extra)
    return m


class Rider:
    """A road cyclist seen from the side, riding to the right, in parts that can move: the wheels
    turn and the legs pedal. (x, y) is where the back wheel touches the ground, k the size
    (1 = wheels 36 across). Drawn back to front: wheels, far leg, bike, near leg, body."""

    WHEEL = 18
    REAR, FRONT = (0, -18), (56, -18)
    BB = (24, -14)           # bottom bracket
    CRANK = 8
    SEAT = (16, -48)         # top of the seat tube
    HIP = (15, -55)
    THIGH, SHIN = 28, 27
    SHOULDER = (48, -70)
    HEAD_TOP, HEAD_BOTTOM = (52, -47), (54, -38)

    def __init__(self, x, y, k=1.0, colour="foreground"):
        self.x, self.y, self.k, self.colour = x, y, k, colour

    def p(self, point):
        return f"{fmt(self.x + point[0] * self.k)},{fmt(self.y + point[1] * self.k)}"

    def at(self, point):
        return self.x + point[0] * self.k, self.y + point[1] * self.k

    def paint(self, width, opacity=None):
        return (stroke(self.colour, width * self.k, opacity)
                + " stroke-linecap='round' stroke-linejoin='round'")

    def hub(self, front):
        return self.at(self.FRONT if front else self.REAR)

    def sectors(self, front, angle=0.0):
        """The inside of a wheel: four faint sectors that turn with it."""
        cx, cy = self.hub(front)
        r = (self.WHEEL - 2.5) * self.k
        out = f"<circle cx='{fmt(cx)}' cy='{fmt(cy)}' r='{fmt(r)}' fill='@{self.colour}' fill-opacity='0.1'/>"
        for i in range(2):
            a = angle + i * 180
            out += f"<path d='{ring_path(cx, cy, r, 0.01, a, a + 90)}' fill='@{self.colour}' fill-opacity='0.22'/>"
        return out + f"<circle cx='{fmt(cx)}' cy='{fmt(cy)}' r='{fmt(2.5 * self.k)}' {theme(self.colour)}/>"

    def leg(self, angle, opacity=None):
        """A leg with its foot on the pedal at `angle` (clockwise from 12 o'clock); the knee forward."""
        bx, by = self.BB
        a = math.radians(angle)
        foot = (bx + self.CRANK * math.sin(a), by - self.CRANK * math.cos(a))
        hx, hy = self.HIP
        dx, dy = foot[0] - hx, foot[1] - hy
        d = min(math.hypot(dx, dy), self.THIGH + self.SHIN - 0.01)
        bend = math.acos((self.THIGH ** 2 + d ** 2 - self.SHIN ** 2) / (2 * self.THIGH * d))
        base = math.atan2(dy, dx)
        knees = [(hx + self.THIGH * math.cos(base + s * bend), hy + self.THIGH * math.sin(base + s * bend)) for s in (1, -1)]
        knee = max(knees, key=lambda q: q[0])
        toe = (foot[0] + 7, foot[1] + 1)
        crank = f"<path d='M{self.p(self.BB)} L{self.p(foot)}' {self.paint(2.5, opacity)}/>"
        # a strong thigh, a slimmer shin and the shoe
        return (crank + f"<path d='M{self.p(self.HIP)} L{self.p(knee)}' {self.paint(11, opacity)}/>"
                + f"<path d='M{self.p(knee)} L{self.p(foot)} L{self.p(toe)}' {self.paint(7, opacity)}/>")

    def bike(self):
        out = ""
        for front in (False, True):
            cx, cy = self.hub(front)
            out += f"<circle cx='{fmt(cx)}' cy='{fmt(cy)}' r='{fmt((self.WHEEL - 1.5) * self.k)}' {self.paint(4.5)}/>"
        # thin stays, a thick down tube and seat tube
        top = (17, -44)
        out += (f"<path d='M{self.p(self.REAR)} L{self.p(self.BB)} M{self.p(self.REAR)} L{self.p(top)}' {self.paint(2.2)}/>"
                f"<path d='M{self.p(self.BB)} L{self.p(self.SEAT)} M{self.p(self.BB)} L{self.p(self.HEAD_BOTTOM)}' {self.paint(4.5)}/>"
                f"<path d='M{self.p(top)} L{self.p(self.HEAD_TOP)} M{self.p(self.HEAD_TOP)} L{self.p(self.HEAD_BOTTOM)}' {self.paint(3.5)}/>"
                f"<path d='M{self.p(self.HEAD_BOTTOM)} L{self.p(self.FRONT)}' {self.paint(3)}/>"
                # a long stem and drop bars
                f"<path d='M{self.p(self.HEAD_TOP)} L{self.p((63, -50))} Q{self.p((71, -49))} {self.p((68, -40))} "
                f"L{self.p((63, -39))}' {self.paint(3)}/>"
                # the saddle and the chainring
                f"<path d='M{self.p((9, -50))} L{self.p((22, -50))}' {self.paint(4)}/>"
                f"<circle cx='{fmt(self.at(self.BB)[0])}' cy='{fmt(self.at(self.BB)[1])}' r='{fmt(6.5 * self.k)}' {theme(self.colour)}/>")
        return out

    def body(self):
        """Back bent low and rounded over the bars, arms to the hoods, a head in a helmet."""
        hx, hy = self.at((57, -75))
        r = 7 * self.k
        # an aero helmet, its tail pointing back
        helmet = (f"M{self.p((42, -81))} Q{self.p((51, -92))} {self.p((62, -87))} Q{self.p((68, -83))} {self.p((65, -77))} "
                  f"L{self.p((52, -76))} Z")
        sx, sy = self.at(self.HIP)
        return (f"<circle cx='{fmt(sx)}' cy='{fmt(sy)}' r='{fmt(7 * self.k)}' {theme(self.colour)}/>"
                f"<path d='M{self.p(self.HIP)} Q{self.p((28, -80))} {self.p(self.SHOULDER)}' {self.paint(13)}/>"
                f"<path d='M{self.p(self.SHOULDER)} L{self.p((54, -57))} L{self.p((63, -50))}' {self.paint(5.5)}/>"
                f"<circle cx='{fmt(hx)}' cy='{fmt(hy)}' r='{fmt(r)}' {theme(self.colour)}/>"
                f"<path d='{helmet}' {theme(self.colour)}/>")

    def still(self, angle=60):
        """All of it in one picture, the pedals at `angle`."""
        return (self.sectors(False) + self.sectors(True) + self.leg(angle + 180, 0.85) + self.bike()
                + self.leg(angle) + self.body())


def strip_of(box, steps, draw):
    """`steps` pictures of what `draw(i)` draws inside `box` (x, y, w, h), side by side: an
    animation strip, (svg, image frame, frame rect)."""
    x, y, w, h = box
    pictures = "".join(f"<g transform='translate({fmt(i * w - x)},{fmt(-y)})'>{draw(i)}</g>" for i in range(steps))
    return svg(w * steps, h, pictures), (x, y, w * steps, h), [x, y, w, h]


def strip_component(file, frame, frame_rect, steps, max_fps, top=None, channel=None):
    m = {"type": "animationStrip", "frames": steps, "minFps": 0, "maxFps": max_fps, "frameRect": frame_rect}
    if top is not None:
        m.update(minValue=0, maxValue=top)
    c = image(file, frame, manipulations=[m])
    if channel:
        c["channel"] = channel
    return c


WHEEL_STEPS = 12     # pictures of a wheel over a quarter turn, where its sectors repeat
LEG_STEPS = 24       # pictures of the legs over a whole turn of the cranks
TOP_CADENCE = 200


def moving_rider(rider, name, size=(400, 400)):
    """Drawings and components of a rider whose wheels turn with the speed and whose legs turn
    with the cadence (at the real rate), on a gauge of `size`."""
    drawings = {}
    components = []
    k = rider.k
    for front in (False, True):
        cx, cy = rider.hub(front)
        r = rider.WHEEL * k
        box = (math.floor(cx - r), math.floor(cy - r), math.ceil(2 * r) + 1, math.ceil(2 * r) + 1)
        file = f"{name}-wheel{'F' if front else 'R'}.svg"
        drawn, frame, rect = strip_of(box, WHEEL_STEPS, lambda i, f=front: rider.sectors(f, 90.0 * i / WHEEL_STEPS))
        drawings[file] = drawn
        # at the ride's top speed a turn and a half a second: clearly turning, never a blur
        components.append(strip_component(file, frame, rect, WHEEL_STEPS, 1.5 * 4 * WHEEL_STEPS, channel="speed"))
    # the legs: from above the hip down to the lowest pedal, toes included
    x0, y0 = rider.at((-4, -64))
    x1, y1 = rider.at((52, 0))
    box = (math.floor(x0), math.floor(y0), math.ceil(x1 - x0), math.ceil(y1 - y0))
    fps = TOP_CADENCE / 60 * LEG_STEPS
    for part, draw in (("far", lambda i: rider.leg(360.0 * i / LEG_STEPS + 180, 0.85)),
                       ("near", lambda i: rider.leg(360.0 * i / LEG_STEPS))):
        file = f"{name}-{part}.svg"
        drawn, frame, rect = strip_of(box, LEG_STEPS, draw)
        drawings[file] = drawn
        components.append(strip_component(file, frame, rect, LEG_STEPS, fps, TOP_CADENCE, "cadence"))
    w, h = size
    drawings[f"{name}-bike.svg"] = svg(w, h, rider.bike())
    drawings[f"{name}-body.svg"] = svg(w, h, rider.body())
    wheels, far, near = components[:2], components[2], components[3]
    return drawings, wheels + [far, image(f"{name}-bike.svg", (0, 0, w, h)), near,
                               image(f"{name}-body.svg", (0, 0, w, h))]


def ride_cluster(name):
    """A round cluster for a ride: the heading on a strip of compass at the top, elevation and grade,
    a rider who leans with the slope, the speed, cadence and elevation as arcs on the sides, power
    and distance at the bottom."""
    defs = ("<radialGradient id='well' cx='0.5' cy='0.4' r='0.6'>"
            "<stop offset='0' stop-color='#000000' stop-opacity='0.45'/>"
            "<stop offset='1' stop-color='#000000' stop-opacity='0.7'/></radialGradient>")
    body = (f"<circle cx='{C}' cy='{C}' r='196' fill='@background' fill-opacity='0.55'/>"
            f"<circle cx='{C}' cy='{C}' r='196' fill='none' stroke='#000000' stroke-opacity='0.4' stroke-width='3'/>"
            # tracks of the strip and the two arcs
            f"<path d='{ring_path(C, C, 176, 146, -50, 50)}' fill='#000000' fill-opacity='0.35'/>"
            f"<path d='{ring_path(C, C, 176, 150, 205, 305)}' fill='#ffffff' fill-opacity='0.07'/>"
            f"<path d='{ring_path(C, C, 176, 150, 55, 155)}' fill='#ffffff' fill-opacity='0.07'/>"
            # the bottom, under power and distance
            f"<path d='{ring_path(C, C, 192, 136, 160, 200)}' fill='#ffffff' fill-opacity='0.05'/>"
            f"<circle cx='{C}' cy='{C}' r='136' fill='url(#well)'/>"
            f"<circle cx='{C}' cy='{C}' r='136' fill='none' stroke='#ffffff' stroke-opacity='0.1' stroke-width='2'/>")
    # a chainring by the cadence arc, a mountain by the elevation arc
    gx, gy = polar(C, C, 163, 214)
    teeth = ticks(gx, gy, 0, 337.5, 16, 9.5, 3, 2.6, "foreground")
    ax, ay = polar(gx, gy, 12, 135)
    bx, by = polar(gx, gy, 12, 315)
    pedals = "".join(f"<rect x='{fmt(px - 5)}' y='{fmt(py - 1.8)}' width='10' height='3.6' "
                     f"transform='rotate(-45 {fmt(px)} {fmt(py)})' {theme('foreground')}/>"
                     for px, py in ((ax, ay), (bx, by)))
    icons = (f"<circle cx='{fmt(gx)}' cy='{fmt(gy)}' r='6.5' {stroke('foreground', 2.6)}/>" + teeth
             + f"<path d='M{fmt(ax)},{fmt(ay)} L{fmt(bx)},{fmt(by)}' {stroke('foreground', 2.6)}/>" + pedals)
    mx, my = polar(C, C, 163, 146)
    # white with a dark edge: it sits on the elevation arc, which is the light theme colour
    icons += (f"<path d='M{fmt(mx - 12)},{fmt(my + 8)} L{fmt(mx - 3)},{fmt(my - 8)} L{fmt(mx + 2)},{fmt(my)} "
              f"L{fmt(mx + 6)},{fmt(my - 5)} L{fmt(mx + 13)},{fmt(my + 8)} Z' fill='#ffffff' "
              "stroke='#000000' stroke-opacity='0.55' stroke-width='1.5' stroke-linejoin='round'/>")
    drawings = {"face.svg": svg(400, 400, body, defs), "icons.svg": svg(400, 400, icons)}

    # the compass strip turns under a mark; only its top shows
    strip = ticks(C, C, 0, 360, 73, 174, 7, 1.4, "foreground", opacity=0.5)
    for label, angle in (("N", 0), ("NE", 45), ("E", 90), ("SE", 135), ("S", 180), ("SW", 225), ("W", 270), ("NW", 315)):
        x, y = polar(C, C, 157, angle)
        strip += (f"<text x='{fmt(x)}' y='{fmt(y + 6)}' font-family='{FONT}' font-size='18' font-weight='bold' "
                  f"text-anchor='middle' transform='rotate({angle} {fmt(x)} {fmt(y)})' "
                  f"{theme('accent' if label == 'N' else 'foreground')}>{label}</text>")
    drawings["strip.svg"] = svg(400, 400, strip)
    drawings["mark.svg"] = svg(400, 400, f"<path d='M{C},{C - 142} L{C - 8},{C - 132} L{C + 8},{C - 132} Z' {theme('accent')}/>")

    arc = ring_path(C, C, 176, 150, 205, 305)
    drawings["cadence.svg"] = svg(400, 400, f"<path d='{arc}' {theme('accent')}/>")
    drawings["climb.svg"] = svg(400, 400, f"<path d='{ring_path(C, C, 176, 150, 55, 155)}' {theme('accent2')}/>")

    # the rider and the road under him lean with the grade; wheels and legs turn
    drawings["road.svg"] = svg(400, 400, f"<path d='M{C - 78},{C + 32} L{C + 78},{C + 32}' {stroke('foreground', 3, 0.6)}/>")
    rider_drawings, rider_parts = moving_rider(Rider(C - 30, C + 31, 1.12), "rider")
    drawings.update(rider_drawings)

    components = [image("face.svg", (0, 0, 400, 400)),
                  dict(image("strip.svg", (0, 0, 400, 400), manipulations=[
                      {"type": "staticRadialClip", "minAngle": -52, "maxAngle": 52, "pivot": [C, C]},
                      rotate(0, -360, (C, C), minValue=0, maxValue=360, wrap=True)]), channel="heading"),
                  image("mark.svg", (0, 0, 400, 400)),
                  text("valueAndUnit", (C, 15), 20, bold=True, precision=0, channel="heading"),
                  dict(image("cadence.svg", (0, 0, 400, 400), manipulations=[radial_clip(205, 305, (C, C))]),
                       channel="cadence"),
                  dict(image("climb.svg", (0, 0, 400, 400), manipulations=[radial_clip(155, 55, (C, C))]),
                       channel="elevation"),
                  image("icons.svg", (0, 0, 400, 400)),
                  text("valueAndUnit", (C, 92), 30, bold=True, upperCase=True, precision=0, channel="elevation"),
                  text("labelValueUnit", (C, 120), 18, "accent2", upperCase=True, precision=0, channel="grade"),
                  dict(group([image("road.svg", (0, 0, 400, 400))] + rider_parts, [
                      rotate(12, -12, (C, C + 32), minValue=-15, maxValue=15)]), channel="grade"),
                  text("valueAndUnit", (C, 268), 42, "accent", bold=True, upperCase=True, precision=0),
                  text("valueAndUnit", (C, 306), 20, upperCase=True, precision=0, channel="power",
                       hideWhenUnknown=True),
                  text("valueAndUnit", (C, 362), 28, bold=True, upperCase=True, precision=1, channel="distance")]
    write(name, (400, 400), components, drawings, ["speed"])


def graph_plate(width, height, top):
    return (f"<rect x='0' y='0' width='{width}' height='{height}' fill='@background' fill-opacity='0.5'/>"
            f"<rect x='0' y='0' width='{width}' height='{top}' fill='#000000' fill-opacity='0.2'/>"
            f"<rect x='0' y='0' width='{width}' height='3' {theme('accent2')}/>")


def graph_knob(colour="foreground", text_at=None, channel=None):
    knob = [image("dot.svg", (-8, -8, 16, 16))]
    if text_at:
        t = text("valueAndUnit", text_at, 20, colour, align="left", italic=True, precision=1)
        if channel:
            t["channel"] = channel
        knob.append(t)
    return knob


def graph(name):
    """The channel over the whole ride as a filled line, a mark where the video is."""
    w, h, top = 520, 200, 44
    drawings = {"plate.svg": svg(w, h, graph_plate(w, h, top)),
                "line.svg": svg(2, 400, "<rect x='0' y='0' width='2' height='400' fill='@accent2' fill-opacity='0.5'/>"),
                "dot.svg": svg(16, 16, f"<circle cx='8' cy='8' r='6' {theme('foreground')}/>"
                                       f"<circle cx='8' cy='8' r='6' fill='none' stroke='#000000' stroke-opacity='0.5' stroke-width='1.5'/>")}
    g = {"type": "graph", "frame": [0, top + 10, w, h - top - 14], "color": "accent", "lineWidth": 3,
         "fillAlpha": 70, "padding": 0,
         "xMarker": [image("line.svg", (-1, 0, 2, h - top - 14))],
         "knob": graph_knob(text_at=(12, -16))}
    components = [image("plate.svg", (0, 0, w, h)),
                  text("label", (14, 23), 22, "accent2", align="left", italic=True, upperCase=True),
                  text("valueAndUnit", (w - 14, 23), 24, align="right", bold=True, precision=1),
                  g]
    write(name, (w, h), components, drawings,
          ["elevation", "speed", "heartRate", "power", "cadence", "grade", "temperature"])


def graph_pair(name):
    """Two channels over the ride, the one placed and heart rate, each with its value on top."""
    w, h, top = 520, 230, 46
    half = w / 2
    plate_ = (f"<rect x='0' y='0' width='{w}' height='{h}' fill='@background' fill-opacity='0.5'/>"
              f"<rect x='0' y='0' width='{fmt(half)}' height='{top}' fill='@accent' fill-opacity='0.55'/>"
              f"<rect x='{fmt(half)}' y='0' width='{fmt(half)}' height='{top}' fill='@accent2' fill-opacity='0.35'/>")
    drawings = {"plate.svg": svg(w, h, plate_),
                "line.svg": svg(2, 400, "<rect x='0' y='0' width='2' height='400' fill='@foreground' fill-opacity='0.5'/>"),
                "dot.svg": svg(16, 16, f"<circle cx='8' cy='8' r='6' {theme('foreground')}/>")}
    area = [0, top + 12, w, h - top - 16]
    first = {"type": "graph", "frame": area, "color": "accent", "lineWidth": 3, "fillAlpha": 60,
             "xMarker": [image("line.svg", (-1, 0, 2, area[3]))], "knob": graph_knob()}
    second = {"type": "graph", "frame": area, "color": "accent2", "lineWidth": 3, "channel": "heartRate",
              "knob": graph_knob()}
    hr_icon = {"type": "image", "channelIcon": True, "channel": "heartRate", "color": "foreground",
               "frame": [half + 12, 9, 28, 28]}
    components = [image("plate.svg", (0, 0, w, h)),
                  {"type": "image", "channelIcon": True, "color": "foreground", "frame": [12, 9, 28, 28]},
                  text("valueAndUnit", (52, 23), 24, align="left", italic=True, precision=1),
                  hr_icon,
                  dict(text("valueAndUnit", (half + 52, 23), 24, align="left", italic=True, precision=0), channel="heartRate"),
                  first, second]
    write(name, (w, h), components, drawings, ["elevation", "speed", "power", "cadence"])


def progress(name):
    """The ride from start to finish as a line, a rider moving along it with the distance."""
    w, h = 600, 130
    x0, x1, line_y = 34, 566, 74
    flag = "".join(f"<rect x='{x1 + 2 + 5 * (i % 3)}' y='{line_y - 26 + 5 * (i // 3)}' width='5' height='5' "
                   f"fill='{'#ffffff' if (i % 3 + i // 3) % 2 else '#111111'}'/>" for i in range(12))
    plate_ = (f"<rect x='0' y='0' width='{w}' height='{h}' rx='6' fill='@background' fill-opacity='0.45'/>"
              f"<path d='M{x0},{line_y} L{x1},{line_y}' {stroke('foreground', 5, 0.25)} stroke-linecap='round'/>"
              f"<path d='M{x1 + 1},{line_y + 3} L{x1 + 1},{line_y - 26}' {stroke('foreground', 2)}/>" + flag)
    drawings = {"plate.svg": svg(w, h, plate_),
                "done.svg": svg(w, h, f"<path d='M{x0},{line_y} L{x1},{line_y}' {stroke('accent', 5)} stroke-linecap='round'/>")}
    # the same rider as on the ride cluster, wheels and legs turning; the road line under the tyres
    k = 0.72
    rider_drawings, rider_parts = moving_rider(Rider(x0 - 28 * k, line_y - 3, k), "rider", (w, h))
    drawings.update(rider_drawings)
    along = dict(range="recording")
    rider = group(rider_parts + [
                   text("valueAndUnit", (x0, line_y + 20), 20, italic=True, precision=1),
                   dict(text("valueAndUnit", (x0, line_y + 41), 17, "accent2", italic=True, precision=0), channel="speed")],
                  [translate(0, x1 - x0, **along)])
    components = [image("plate.svg", (0, 0, w, h)),
                  image("done.svg", (0, 0, w, h), manipulations=[{"type": "widthClip", "minX": x0 - 3, "maxX": x1, **along}]),
                  rider]
    write(name, (w, h), components, drawings, ["distance"])


def climb_wedge(name):
    """A slanted plate with a wedge that fills along the channel's scale, and the value large."""
    w, h = 480, 220
    slant = 26
    plate_ = (f"<path d='M{slant},0 L{w},0 L{w - slant},{h} L0,{h} Z' fill='@background' fill-opacity='0.5'/>"
              f"<rect x='{slant + 6}' y='0' width='{w - slant - 6}' height='5' {theme('accent2')}/>")
    wedge = f"M60,190 L420,190 L420,138 Z"
    drawings = {"plate.svg": svg(w, h, plate_),
                "wedge.svg": svg(w, h, f"<path d='{wedge}' fill='#ffffff' fill-opacity='0.3'/>"),
                "fill.svg": svg(w, h, f"<path d='{wedge}' {theme('accent')}/>"),
                "mark.svg": svg(w, h, f"<rect x='57' y='132' width='6' height='62' {theme('foreground')}/>")}
    along = {}   # the channel's scale, like a dial's
    components = [image("plate.svg", (0, 0, w, h)), image("wedge.svg", (0, 0, w, h)),
                  image("fill.svg", (0, 0, w, h), manipulations=[{"type": "widthClip", "minX": 60, "maxX": 420, **along}]),
                  image("mark.svg", (0, 0, w, h), manipulations=[translate(0, 360, **along)]),
                  text("value", (360, 80), 84, align="right", bold=True, italic=True, precision=0),
                  text("unit", (370, 92), 38, "accent2", align="left", italic=True)]
    write(name, (w, h), components, drawings, ["elevation", "totalAscent", "distance"])


def rhombus(cy, half_w=100, half_h=40, cx=150):
    return f"M{cx - half_w},{cy} L{cx},{cy - half_h} L{cx + half_w},{cy} L{cx},{cy + half_h} Z"


def column(name):
    """A glass box seen from above at an angle; a level rises in it along the channel's scale,
    the part below it filled."""
    w, h = 300, 420
    top, bottom = 70, 290
    left_face = f"M50,{top} L150,{top + 40} L150,{bottom + 40} L50,{bottom} Z"
    right_face = f"M150,{top + 40} L250,{top} L250,{bottom} L150,{bottom + 40} Z"
    edges = stroke('accent2', 2.5) + " stroke-linejoin='round'"
    box = (f"<path d='{left_face}' fill='@background' fill-opacity='0.55'/>"
           f"<path d='{right_face}' fill='@background' fill-opacity='0.4'/>"
           f"<path d='{rhombus(top)}' fill='@background' fill-opacity='0.3'/>"
           f"<rect x='90' y='356' width='120' height='44' rx='4' fill='@background' fill-opacity='0.6'/>")
    def face(path, x):
        # drawn into a frame of its own, so the slanted clip turns about the face's middle
        return svg(100, h, f"<g transform='translate({-x},0)'><path d='{path}' fill='@accent' fill-opacity='0.35'/></g>")
    # a little inside the box, so the glass edges drawn over it hide its outer half
    level = (f"<path d='{rhombus(bottom, 90, 36)}' fill='#ffffff' fill-opacity='0.25'/>"
             f"<path d='{rhombus(bottom, 97.5, 39)}' {stroke('accent', 4)} stroke-linejoin='round'/>")
    glass = (f"<path d='{rhombus(top)}' {edges}/>"
             f"<path d='M50,{top} L50,{bottom} L150,{bottom + 40} L250,{bottom} L250,{top} M150,{top + 40} L150,{bottom + 40}' {edges}/>")
    drawings = {"box.svg": svg(w, h, box), "left.svg": face(left_face, 50), "right.svg": face(right_face, 150),
                "level.svg": svg(w, h, level), "edges.svg": svg(w, h, glass)}
    along = {}   # the channel's scale, like a dial's
    # the level's front edges meet the faces' middles 20 below its centre
    angle = round(math.degrees(math.atan2(40, 100)), 2)
    rise = {"type": "heightClip", "minY": bottom + 20, "maxY": top + 20, **along}
    components = [image("box.svg", (0, 0, w, h)),
                  image("left.svg", (50, 0, 100, h), manipulations=[dict(rise, clipAngle=angle)]),
                  image("right.svg", (150, 0, 100, h), manipulations=[dict(rise, clipAngle=-angle)]),
                  image("level.svg", (0, 0, w, h), manipulations=[translate(0, 0, 0, top - bottom, **along)]),
                  image("edges.svg", (0, 0, w, h)),
                  text("valueAndUnit", (150, 378), 28, italic=True, precision=0)]
    write(name, (w, h), components, drawings, ["elevation", "power", "speed", "heartRate", "cadence"])


CRANK_STEPS = 36    # pictures of the crank over half a turn, 5 degrees apart


def crank(name):
    """Two pedals going round as fast as the rider pedals, the cadence in the middle."""
    s, c = 300, 150
    # a grey rim, lighter at the top left
    defs = ("<linearGradient id='rim' x1='0' y1='0' x2='1' y2='1'>"
            "<stop offset='0' stop-color='#c4c4c4'/><stop offset='0.5' stop-color='#7a7a7a'/>"
            "<stop offset='1' stop-color='#3c3c3c'/></linearGradient>")
    face = (f"<circle cx='{c}' cy='{c}' r='142' fill='@background' fill-opacity='0.55'/>"
            f"<circle cx='{c}' cy='{c}' r='142' fill='none' stroke='url(#rim)' stroke-width='3'/>"
            f"<circle cx='{c}' cy='{c}' r='112' fill='none' stroke='#ffffff' stroke-opacity='0.08' stroke-width='34'/>")
    hub = (f"<circle cx='{c}' cy='{c}' r='82' fill='#000000' fill-opacity='0.3'/>"
           f"<circle cx='{c}' cy='{c}' r='82' fill='none' stroke='#ffffff' stroke-opacity='0.1' stroke-width='2'/>")
    # the pedals repeat every half turn: one strip of pictures covers it
    pictures = []
    for i in range(CRANK_STEPS):
        angle = 180.0 * i / CRANK_STEPS
        x0, y0 = polar(c, c, 112, angle)
        x1, y1 = polar(c, c, 112, angle + 180)
        pictures.append(f"<g transform='translate({i * s},0)'>"
                        f"<circle cx='{fmt(x0)}' cy='{fmt(y0)}' r='19' {theme('accent')}/>"
                        f"<circle cx='{fmt(x1)}' cy='{fmt(y1)}' r='19' {theme('accent')}/></g>")
    drawings = {"face.svg": svg(s, s, face, defs), "hub.svg": svg(s, s, hub),
                "pedals.svg": svg(s * CRANK_STEPS, s, "".join(pictures))}
    # 1 rpm = 2 half turns a minute = CRANK_STEPS / 30 pictures a second
    top = 200
    strip = {"type": "animationStrip", "frames": CRANK_STEPS, "minFps": 0, "maxFps": top * CRANK_STEPS / 30,
             "frameRect": [0, 0, s, s], "minValue": 0, "maxValue": top}
    components = [image("face.svg", (0, 0, s, s)),
                  image("pedals.svg", (0, 0, s * CRANK_STEPS, s), manipulations=[strip]),
                  image("hub.svg", (0, 0, s, s)),
                  text("value", (c, 140), 72, bold=True, precision=0),
                  text("unit", (c, 192), 26, "accent2")]
    write(name, (s, s), components, drawings, ["cadence"])


def map_gauge(name, lap):
    """The route (or the lap under way) as a line; where the rider is, an arrow pointing the way
    with the speed and the distance beside it."""
    s = 420
    arrow = (f"<path d='M20,6 L33,34 L20,27 L7,34 Z' {theme('accent')} stroke='#000000' stroke-opacity='0.4' "
             "stroke-width='1.5' stroke-linejoin='round'/>")
    ring = (f"<path d='{ring_path(20, 20, 20, 16.5, -150, 60)}' fill='@foreground' fill-opacity='0.55'/>")
    drawings = {"arrow.svg": svg(40, 40, arrow), "ring.svg": svg(40, 40, ring)}
    knob = [image("ring.svg", (-20, -20, 40, 40)),
            dict(image("arrow.svg", (-20, -20, 40, 40), manipulations=[
                rotate(0, 360, (0, 0), minValue=0, maxValue=360, wrap=True)]), channel="heading"),
            dict(text("valueAndUnit", (16, 26), 15, align="left", italic=True, upperCase=True, precision=0), channel="speed"),
            dict(text("valueAndUnit", (16, 44), 15, align="left", italic=True, upperCase=True, precision=1), channel="distance")]
    route = {"type": "lapMap" if lap else "trackMap", "frame": [24, 24, s - 48, s - 48], "color": "foreground",
             "lineWidth": 5, "padding": 20, "knob": knob}
    if lap:
        squares = "".join(f"<rect x='{4 * (i % 8)}' y='{4 * (i // 8)}' width='4' height='4' "
                          f"fill='{'#ffffff' if (i % 8 + i // 8) % 2 else '#111111'}'/>" for i in range(24))
        drawings["start.svg"] = svg(32, 12, squares)
        route["startLine"] = [image("start.svg", (-16, -6, 32, 12))]
    write(name, (s, s), [route], drawings, ["speed"])


def half_dial(name, side):
    """Half a dial on one side, filled up to the value; its dark band runs on a little past the
    start of the scale and carries the value there."""
    w, h, cx, cy = 380, 400, 190, 200
    start, end = (-150, 30) if side == "left" else (150, -30)
    lo, hi = min(start, end), max(start, end)
    # the scale's band and a short tail past its start, under the value
    band = (145, 390) if side == "left" else (-30, 215)
    face = (f"<path d='{ring_path(cx, cy, 188, 104, *band)}' fill='@background' fill-opacity='0.6'/>"
            f"<path d='{ring_path(cx, cy, 188, 184, lo, hi)}' fill='#ffffff' fill-opacity='0.25'/>"
            f"<path d='{ring_path(cx, cy, 168, 110, lo, hi)}' fill='#ffffff' fill-opacity='0.06'/>")
    scale = ticks(cx, cy, start, end, 7, 182, 14, 4) + ticks(cx, cy, start, end, 31, 182, 11, 1.4, skip_every=5, opacity=0.6)
    # the fill reaches up to the ticks, the numbers sit on it; darker towards the middle
    shade = (f"<radialGradient id='shade' gradientUnits='userSpaceOnUse' cx='{cx}' cy='{cy}' r='168'>"
             "<stop offset='0.65' stop-color='#000000' stop-opacity='0.5'/>"
             "<stop offset='1' stop-color='#000000' stop-opacity='0'/></radialGradient>")
    drawings = {"face.svg": svg(w, h, face), "scale.svg": svg(w, h, scale),
                "fill.svg": svg(w, h, f"<path d='{ring_path(cx, cy, 168, 110, lo, hi)}' {theme('accent')}/>"
                                      f"<path d='{ring_path(cx, cy, 168, 110, lo, hi)}' fill='url(#shade)'/>", shade)}
    # in the middle of the tail
    vx = 196 if side == "left" else w - 196
    components = [image("face.svg", (0, 0, w, h)),
                  image("fill.svg", (0, 0, w, h), manipulations=[radial_clip(start, end, (cx, cy))]),
                  image("scale.svg", (0, 0, w, h))]
    components += markings(cx, cy, 147, start, end, 7, 20, italic=True)
    components += [text("value", (vx, 332), 48, bold=True, italic=True, precision=0),
                   text("unit", (vx, 367), 20, "accent2", italic=True)]
    write(name, (w, h), components, drawings, ROUND_TYPES)


def tape(name):
    """A tall scale that slides past a fixed mark, the value in a box on the mark."""
    w, h = 170, 640
    defs = ("<linearGradient id='fade' x1='0' y1='0' x2='0' y2='1'>"
            "<stop offset='0' stop-color='#000000' stop-opacity='0'/>"
            "<stop offset='0.12' stop-color='#000000' stop-opacity='1'/>"
            "<stop offset='0.88' stop-color='#000000' stop-opacity='1'/>"
            "<stop offset='1' stop-color='#000000' stop-opacity='0'/></linearGradient>")
    plate_ = (f"<rect x='20' y='44' width='96' height='{h - 44}' fill='url(#fade)' fill-opacity='0.45'/>"
              f"<rect x='18' y='54' width='2' height='{h - 74}' {theme('accent')}/>")
    middle = 44 + (h - 44) / 2
    mark = (f"<path d='M8,{fmt(middle - 10)} L22,{fmt(middle)} L8,{fmt(middle + 10)} Z' {theme('accent')}/>"
            f"<rect x='24' y='{fmt(middle - 21)}' width='{w - 26}' height='42' rx='3' fill='@background' fill-opacity='0.95'/>"
            f"<rect x='24' y='{fmt(middle - 21)}' width='{w - 26}' height='42' rx='3' fill='none' {theme('accent', 'stroke')} stroke-width='2'/>")
    drawings = {"plate.svg": svg(w, h, plate_, defs), "mark.svg": svg(w, h, mark)}
    length = h - 104
    scale = {"type": "tape", "frame": [20, 74 - 12, 96, length], "color": "foreground",
             "font": {"family": FONT, "size": 18, "italic": True}, "align": "left", "valign": "center",
             "scale": {"minValue": -500, "maxValue": 9000, "orientation": "numbersRight", "length": length,
                       "spacing": 70, "labelOffset": 8,
                       "major": {"unit": 10, "length": 18, "width": 2.5, "color": "accent2"},
                       "minor": {"unit": 2, "length": 9, "width": 1.2, "color": "accent2"}}}
    components = [image("plate.svg", (0, 0, w, h)), scale, image("mark.svg", (0, 0, w, h)),
                  text("valueAndUnit", (w - 10, middle), 26, align="right", bold=True, precision=0),
                  text("label", (20, 20), 20, "accent2", align="left", italic=True, upperCase=True)]
    write(name, (w, h), components, drawings, ["elevation"])


def g_rings(name):
    """Rings at a quarter and half a g (as much as a bicycle gets), lines every 45 degrees, and a
    ball pushed the way the rider is: back when speeding up, outwards in a bend."""
    face = (f"<circle cx='{C}' cy='{C}' r='186' fill='@background' fill-opacity='0.2'/>"
            f"<circle cx='{C}' cy='{C}' r='79' fill='#ffffff' fill-opacity='0.08'/>"
            f"<circle cx='{C}' cy='{C}' r='79' {stroke('accent2', 6, 0.6)}/>")
    for i in range(8):
        a = i * 45 + 2
        face += f"<path d='{ring_path(C, C, 186, 172, a, a + 41)}' fill='@accent2' fill-opacity='0.6'/>"
    face += ticks(C, C, 0, 315, 8, 172, 172, 1.5, "foreground", opacity=0.25)
    ball = f"<circle cx='22' cy='22' r='20' {theme('accent')}/><circle cx='22' cy='22' r='20' fill='none' stroke='#000000' stroke-opacity='0.35' stroke-width='2'/>"
    drawings = {"face.svg": svg(400, 400, face), "ball.svg": svg(44, 44, ball)}
    components = [image("face.svg", (0, 0, 400, 400)),
                  text("fixed", (C + 79, C - 16), 20, "accent2", italic=True, text="0.25"),
                  text("fixed", (C + 168, C - 16), 20, "accent2", align="right", italic=True, text="0.5 g"),
                  image("ball.svg", (C - 22, C - 22, 44, 44), manipulations=[
                      {"type": "translate2d", "maxRadius": 158, "xChannel": "gForceLateral",
                       "yChannel": "gForceLongitudinal", "minValue": -0.5, "maxValue": 0.5, "invertY": True}])]
    write(name, (400, 400), components, drawings, ["gForceLateral", "gForceLongitudinal"])


def main():
    os.makedirs(GAUGES, exist_ok=True)
    # numbers every major tick, the needle only outside the inner disc
    # no rim: long ticks reaching a little past the face's edge, the thin ones see-through
    round_dial("Dial", bezel=None, needle_style="outer", labels=13, label_radius=124, label_size=19, value_y=196,
               tick_outer=171, major_length=32, minor_length=18, minor_width=1.2, minor_opacity=0.5,
               needle_base=75, needle_tip=164)
    # a thin rim, the ticks reaching out over a shade to it, the thin ones see-through
    metal_ticks = dict(bezel="thin metal", tick_outer=168, major_length=30, minor_length=18, minor_width=1.2,
                       minor_opacity=0.5)
    round_dial("Dial metal", sweep="silver", needle_style="outer", labels=13, label_radius=124, label_size=19,
               value_y=196, needle_base=75, needle_tip=168, needle_inverted=True, **metal_ticks)
    round_dial("Dial metal arc", sweep="silver+accent", needle_style="pointer", labels=13, label_radius=124,
               label_size=19, value_y=186, second_readout=True, needle_base=75, needle_tip=168, **metal_ticks)
    round_dial("Dial ring", bezel=None, labels=7, needle_style="outer", unit_caps=True, value_size=72,
               major_length=18, minor_length=14, minor_width=1.2, minor_opacity=0.5, needle_base=75)
    # the value low, clear of the needle, which starts in the middle without a pin
    round_dial("Dial sport", bezel="shade", face="sport", needle_style="glow", inner_disc=False, value_y=284,
               value_size=60, unit_gap=0.6, major_length=18, minor_length=14, minor_width=1.2, minor_opacity=0.5,
               tick_outer=168)
    round_dial("Dial glass", bezel="rim", gloss_on="half", needle_style="taper", inner_disc=False, labels=13,
               label_radius=130, label_size=26, label_italic=False, value_y=300, value_size=50, unit_gap=0.6,
               tick_outer=165, major_length=11, minor_length=7, minor_width=1.2, minor_opacity=0.5)
    segmented_dial("Dial small")
    small_metal_dial("Dial small metal")
    open_arc("Open arc")
    compass("Compass rose")
    heart("Heart")
    tile("Tile")
    tile("Tile speed", "speed", ["speed"])
    tile("Tile elevation", "elevation", ["elevation"])
    tile("Tile heading", "heading", ["heading"])
    tile("Tile grade", "grade", ["grade"])
    tile("Tile cadence", "arc", ["cadence", "power", "engineRpm", "heartRate"])
    ride_cluster("Ride cluster")
    graph("Graph")
    graph_pair("Graph pair")
    progress("Progress")
    climb_wedge("Climb wedge")
    column("Column glass")
    crank("Crank")
    map_gauge("Route map arrow", lap=False)
    map_gauge("Lap map arrow", lap=True)
    half_dial("Half dial left", "left")
    half_dial("Half dial right", "right")
    tape("Tape elevation")
    g_rings("G-force rings")


if __name__ == "__main__":
    main()
