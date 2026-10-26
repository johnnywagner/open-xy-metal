#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of fifteen pieces each (node 15 r + i): four quarter arcs of radius 8, from the left round to the top
(display.cpp turns them for the other corners), then four vertical pieces 64 px long and two 32 px long that
display.cpp turns across; then the gradient: five discs, grey to white. All start parked off screen. The palette is 16 white strokes, 0.375 px x 1.284^i wide:
display.cpp gives each piece the one that undoes its scale (WIDTHS there is this table in Q10).
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 15, 8
# The gradient's steps, outside in: the OP-XY's own greys (its screen header's steps), then white.
GREYS = ['#323232', '#4B4B4B', '#7D7D7D', '#AFAFAF', '#FFFFFF']
WIDTHS = [0.375 * 1.284 ** i for i in range(16)]

def num(v):
    t = f'{v:.4f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def arc():
    pts = [(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]]
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, fifteen pieces each: four quarter arcs, four pieces down the sides, two across, five discs. -->']
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
            # the gradient, keeping its own paint: five discs, outside in, radius 16 x (5 - j) / 5, filled grey to white
            j = i - 10
            out.append(f'  <circle id="metal_node_{n}" {PARK} cx="0" cy="0" r="{num(16 * (5 - j) / 5)}" '
                       f'fill="{GREYS[j]}" stroke="none"/>')
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
