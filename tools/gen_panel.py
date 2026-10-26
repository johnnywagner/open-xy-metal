#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of 20 pieces each (node 20 r + i), drawn in this order, so the outline lies over the fill:
  0-9   the fill: ten capsules with round ends, TALL[i] times as tall as wide (display.cpp overlaps two end to end to
        make any height exactly; the palette colours them), each wide enough for the fills it makes at scale 2;
  10-13 four quarter arcs of radius 8, from the left round to the top (display.cpp turns them for the other corners);
  14-17 four vertical pieces 64 px long; 18-19 two 32 px long that display.cpp turns across.
All start parked off screen. The palette: twelve white strokes 0.375 px x 1.405^i wide (display.cpp gives each stroked
piece the one that undoes its scale; WIDTHS there is this table in Q10), then the fill's four colours: the OP-XY's
dark, mid and light greys (its screen header's steps) and white.
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 20, 8
TALL = [1, 1.25, 1.5, 1.75, 2.25, 3, 4.25, 6.25, 9.5, 14.75]   # each the two before together, less 1
WIDE = [40, 40, 40, 40, 40, 40, 40, 32, 20, 13.5]               # px: FILL_WIDE_INV in display.cpp
WIDTHS = [0.375 * 1.405 ** i for i in range(12)]
FILLS = ['#4B4B4B', '#7D7D7D', '#AFAFAF', '#FFFFFF']

def num(v, places=2):
    t = f'{v:.{places}f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def path(pts, close=False, places=2):
    return 'M' + ' L'.join(f'{num(x, places)},{num(y, places)}' for x, y in pts) + (' Z' if close else '')

def arc():
    return path([(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]])

def capsule(tall, wide):
    # wide px wide, wide x tall px tall, centred: half circles joined by straight sides (one point each 22.5 degrees
    # round the wider ones, 30 round the narrow)
    r = wide / 2
    half = r * tall - r
    step = 22.5 if wide > 24 else 30
    angles = [math.radians(i * step) for i in range(round(180 / step) + 1)]
    top = [(r * math.cos(a), -half - r * math.sin(a)) for a in angles]
    bottom = [(-r * math.cos(a), half + r * math.sin(a)) for a in angles]
    if half < 0.01: return path(top + bottom[1:-1], True, 1)
    return path(top + bottom, True, 1)

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, 20 pieces each: the fill (ten capsules), then the outline (four quarter arcs, four',
       '       pieces down the sides, two across). -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 10:
            d = capsule(TALL[i], WIDE[i])
            out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="#000000" stroke="none"/>')
            continue
        if i < 14:
            d, cap = arc(), 'round" stroke-linejoin="round'
        elif i < 18:
            d, cap = 'M0,-32 L0,32', 'square'
        else:
            d, cap = 'M0,-16 L0,16', 'square'
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="#FFFFFF" stroke-width="1.3" '
                   f'stroke-linecap="{cap}"/>')
out.append('  <!-- The palette: twelve white strokes, 0.375 px x 1.405^i wide; then the fill\'s four colours. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="#FFFFFF" '
               f'stroke-width="{w:.4f}"/>')
for j, colour in enumerate(FILLS):
    out.append(f'  <circle id="metal_palette_{12 + j}" {PARK} cx="0" cy="0" r="1" fill="{colour}" stroke="{colour}" '
               f'stroke-width="0.6"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
