#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of nineteen pieces each (node 19 r + i): four quarter arcs of radius 8, from the left round to the top
(display.cpp turns them for the other corners), then four vertical pieces 64 px long and two 32 px long that
display.cpp turns across; then the sheen: a stripe and a glint in four colours, and a rim. All start parked off screen. The palette is 16 white strokes, 0.375 px x 1.284^i wide:
display.cpp gives each piece the one that undoes its scale (WIDTHS there is this table in Q10).
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 19, 8
# The sheen's colours, dark to light: the OP-XY's own greys (its screen header's dark, mid and light steps), then white.
GREYS = ['#4B4B4B', '#7D7D7D', '#AFAFAF', '#FFFFFF']
WIDTHS = [0.375 * 1.284 ** i for i in range(16)]

def num(v):
    t = f'{v:.4f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def arc():
    pts = [(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]]
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, nineteen pieces each: four quarter arcs, four pieces down the sides, two across, nine of sheen. -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 4:
            d, cap = arc(), 'round'
        elif i < 8:
            d, cap = 'M0,-32 L0,32', 'square'
        elif i < 10:
            d, cap = 'M0,-16 L0,16', 'square'
        else:
            # the sheen, keeping its own paint: the stripe (32 px, 1.5 px wide) and the glint (radius 8, 1.3 px) in
            # each colour, then the rim (radius 8, 0.8 px, dark grey)
            j = i - 10
            if j < 4:
                d, colour, w = 'M0,-16 L0,16', GREYS[j], '1.5'
            elif j < 8:
                d, colour, w = arc(), GREYS[j - 4], '1.3'
            else:
                d, colour, w = arc(), GREYS[0], '0.8'
            out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="{colour}" stroke-width="{w}" '
                       f'stroke-linecap="round" stroke-linejoin="round"/>')
            continue
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="#FFFFFF" stroke-width="1.3" '
                   f'stroke-linecap="{cap}" stroke-linejoin="round"/>')
out.append('  <!-- The palette: white strokes, 0.375 px x 1.284^i wide. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="#FFFFFF" '
               f'stroke-width="{num(w)}"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
