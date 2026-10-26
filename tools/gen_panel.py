#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of 21 pieces each (node 21 r + i), drawn in this order, so the outline lies over the fill:
  0-8   the fill's three grey steps, outside in, each as three capsules 20 px wide and 1, 4 and 16 times as tall,
        filled grey and kept as drawn (display.cpp shows the one or two that best fit each step's box);
  9     a white disc 20 px across, the fill's core in a round ring;
  10    a 64 px white line with round ends, the core in a tall ring (the palette gives its width);
  11-14 four quarter arcs of radius 8, from the left round to the top (display.cpp turns them for the other corners);
  15-18 four vertical pieces 64 px long; 19-20 two 32 px long that display.cpp turns across.
All start parked off screen. The palette is 16 white strokes, 0.375 px x 1.284^i wide: display.cpp gives each stroked
piece the one that undoes its scale (WIDTHS there is this table in Q10).
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 21, 8
# The fill's grey steps, outside in: the OP-XY's own greys (its screen header's steps). The core is white.
GREYS = ['#323232', '#646464', '#AFAFAF']
TALL = [1, 4, 16]                       # each capsule's height for its width
WIDTHS = [0.375 * 1.284 ** i for i in range(16)]

def num(v):
    t = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def arc():
    pts = [(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]]
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

def capsule(tall):
    # 20 px wide, 20 x tall px tall, centred: half circles of radius 10 joined by straight sides
    half = 10 * tall - 10
    top = [(10 * math.cos(math.radians(a)), -half - 10 * math.sin(math.radians(a))) for a in range(0, 181, 30)]
    bottom = [(-10 * math.cos(math.radians(a)), half + 10 * math.sin(math.radians(a))) for a in range(0, 181, 30)]
    pts = top + bottom
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts) + ' Z'

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, 21 pieces each: the fill (nine grey capsules, a white disc, a white line), then the',
       '       outline (four quarter arcs, four pieces down the sides, two across). -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 9:
            out.append(f'  <path id="metal_node_{n}" {PARK} d="{capsule(TALL[i % 3])}" fill="{GREYS[i // 3]}" stroke="none"/>')
            continue
        if i == 9:
            out.append(f'  <circle id="metal_node_{n}" {PARK} cx="0" cy="0" r="10" fill="#FFFFFF" stroke="none"/>')
            continue
        if i == 10:
            d, cap = 'M0,-32 L0,32', 'round'
        elif i < 15:
            d, cap = arc(), 'round'
        elif i < 19:
            d, cap = 'M0,-32 L0,32', 'square'
        else:
            d, cap = 'M0,-16 L0,16', 'square'
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="#FFFFFF" stroke-width="1.3" '
                   f'stroke-linecap="{cap}" stroke-linejoin="round"/>')
out.append('  <!-- The palette: white strokes, 0.375 px x 1.284^i wide. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="#FFFFFF" '
               f'stroke-width="{w:.4f}"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
