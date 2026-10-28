#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of 10 pieces each (node 10 r + i): four quarter arcs of radius 8, from the left round to the top
(display.cpp turns them for the other corners); four vertical pieces 64 px long; two 32 px long that display.cpp turns
across. Then 96 filled squares 16 px wide with corners rounded CORNER px, two for each of a ring's four boxes (node
120 + 8 r + 2 i + e: top, bottom, left, right; each end): they round the ends of the boxes the OP-XY draws, or one
stands in for a box as a marker (display.cpp turns it for a diamond). All start parked off screen. The palette: twelve strokes 0.375 px x 1.405^i wide (display.cpp gives each
piece the one that shows its width, on the OP-XY or in the browser; WIDTHS there is this table in Q10), then the markers'
fill. Every stroke is COLOUR: try others live in the
screen lab (tools/screen_lab.py), then set it here and rerun this.
"""
import math
from pathlib import Path

RINGS, PIECES, ARC, MARK = 12, 10, 8, 8
COLOUR = '#CDCDCD'                  # the rings' stroke and the markers' fill, #RRGGBB
CORNER = 4                          # the rounded squares' corner radius, px of their 16 (0 square .. 8 round)
WIDTHS = [0.375 * 1.405 ** i for i in range(12)]

def num(v):
    t = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def path(pts):
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

def rounded(radius):
    """A square 2 MARK wide with corners of `radius`, each four straight steps; every point distinct."""
    c, pts = MARK - radius, []
    for cx, cy, start in ((c, -c, -90), (c, c, 0), (-c, c, 90), (-c, -c, 180)):
        for k in range(5):
            t = math.radians(start + k * 90 / 4)
            pt = (round(cx + radius * math.cos(t), 2), round(cy + radius * math.sin(t), 2))
            if not pts or pt != pts[-1]:
                pts.append(pt)
    if pts[-1] == pts[0]:
        pts.pop()
    return path(pts) + ' Z'

def arc():
    return path([(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]])

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, 10 pieces each: the outline (four quarter arcs, four',
       '       pieces down the sides, two across). -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 4:
            d, cap = arc(), 'round" stroke-linejoin="round'
        elif i < 8:
            d, cap = 'M0,-32 L0,32', 'square'
        else:
            d, cap = 'M0,-16 L0,16', 'square'
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="{COLOUR}" stroke-width="1.3" '
                   f'stroke-linecap="{cap}"/>')
out.append(f'  <!-- Rounded squares: two for each of a ring\'s four boxes, corners {CORNER} px. -->')
for n in range(RINGS * 8):
    out.append(f'  <path id="metal_node_{RINGS * PIECES + n}" {PARK} d="{rounded(CORNER)}" fill="{COLOUR}" '
               f'stroke="{COLOUR}" stroke-width="1" stroke-linejoin="round"/>')
out.append(f'  <!-- The palette: twelve {COLOUR} strokes, 0.375 px x 1.405^i wide. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="{COLOUR}" '
               f'stroke-width="{w:.4f}"/>')
out.append(f'  <!-- 12: the markers, filled, with a 1 px edge. -->')
out.append(f'  <circle id="metal_palette_12" {PARK} cx="0" cy="0" r="1" fill="{COLOUR}" stroke="{COLOUR}" stroke-width="1"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
