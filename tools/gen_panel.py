#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of 17 pieces each (node 17 r + i), drawn in this order, so the outline lies over the fill:
  0-6   the fill: capsules 20 px wide with round ends, two as tall as wide, two twice as tall, three four times as
        tall (display.cpp overlaps copies of one end to end to make the ring's height; the palette colours them);
  7-10  four quarter arcs of radius 8, from the left round to the top (display.cpp turns them for the other corners);
  11-14 four vertical pieces 64 px long; 15-16 two 32 px long that display.cpp turns across.
All start parked off screen. The palette: twelve white strokes 0.375 px x 1.405^i wide (display.cpp gives each stroked
piece the one that undoes its scale; WIDTHS there is this table in Q10), then the fill's four colours: the OP-XY's
dark, mid and light greys (its screen header's steps) and white.
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 17, 8
TALL = [1, 1, 2, 2, 4, 4, 4]          # the fill's capsules: two of each, three of the tallest
WIDTHS = [0.375 * 1.405 ** i for i in range(12)]
FILLS = ['#4B4B4B', '#7D7D7D', '#AFAFAF', '#FFFFFF']

def num(v):
    t = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def path(pts, close=False):
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts) + (' Z' if close else '')

def arc():
    return path([(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]])

def capsule(tall):
    # 20 px wide, 20 x tall px tall, centred: half circles of radius 10 joined by straight sides
    half = 10 * tall - 10
    top = [(10 * math.cos(math.radians(a)), -half - 10 * math.sin(math.radians(a))) for a in range(0, 181, 30)]
    bottom = [(-10 * math.cos(math.radians(a)), half + 10 * math.sin(math.radians(a))) for a in range(0, 181, 30)]
    return path(top + bottom, True)

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, 17 pieces each: the fill (seven capsules), then the outline (four quarter arcs, four',
       '       pieces down the sides, two across). -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 7:
            d = capsule(TALL[i])
            out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="#000000" stroke="none"/>')
            continue
        if i < 11:
            d, cap = arc(), 'round'
        elif i < 15:
            d, cap = 'M0,-32 L0,32', 'square'
        else:
            d, cap = 'M0,-16 L0,16', 'square'
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="#FFFFFF" stroke-width="1.3" '
                   f'stroke-linecap="{cap}" stroke-linejoin="round"/>')
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
