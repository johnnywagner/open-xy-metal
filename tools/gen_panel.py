#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of fourteen pieces each (node 14 r + i): four quarter arcs of radius 8, from the left round to the top
(display.cpp turns them for the other corners), then four vertical pieces 64 px long and two 32 px long that
display.cpp turns across; then the sheen: three grey lenses 64 px tall and a 64 px white hairline. All start parked off screen. The palette is 16 white strokes, 0.375 px x 1.284^i wide:
display.cpp gives each piece the one that undoes its scale (WIDTHS there is this table in Q10).
"""
import math
from pathlib import Path

RINGS, PIECES, ARC = 12, 14, 8
# The sheen's slivers: how tall for how wide (display.cpp SLIVER) and their greys, the OP-XY's own (its screen
# header's dark, mid and light steps).
SLIVERS = [(6, '#4B4B4B'), (9, '#7D7D7D'), (16, '#AFAFAF')]
WIDTHS = [0.375 * 1.284 ** i for i in range(16)]

def num(v):
    t = f'{v:.4f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def lens(tall):
    # 64 px tall, 64 / tall wide at its middle, pointed at both ends
    ys = [-32 + 4 * k for k in range(17)]
    right = [(32 / tall * (1 - (y / 32) ** 2), y) for y in ys]
    pts = right + [(-x, y) for x, y in reversed(right[1:-1])]
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts) + ' Z'

def arc():
    pts = [(-ARC * math.cos(math.radians(t)), -ARC * math.sin(math.radians(t))) for t in [i * 90 / 8 for i in range(9)]]
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

PARK = 'transform="matrix(1 0 0 1 640 320)"'
out = ['<g id="metal_panel">', '  <rect x="0" y="20" width="480" height="200" fill="#000000"/>',
       '  <!-- Twelve rings, fourteen pieces each: four quarter arcs, four pieces down the sides, two across, four of sheen. -->']
for r in range(RINGS):
    for i in range(PIECES):
        n = r * PIECES + i
        if i < 4:
            d, cap = arc(), 'round'
        elif i < 8:
            d, cap = 'M0,-32 L0,32', 'square'
        elif i < 10:
            d, cap = 'M0,-16 L0,16', 'square'
        elif i < 13:
            tall, grey = SLIVERS[i - 10]          # the sheen: a lens 64 px tall, filled grey, no outline
            out.append(f'  <path id="metal_node_{n}" {PARK} d="{lens(tall)}" fill="{grey}" stroke="none"/>')
            continue
        else:
            d, cap = 'M0,-32 L0,32', 'round'      # the sheen's hairline
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="#FFFFFF" stroke-width="1.3" '
                   f'stroke-linecap="{cap}" stroke-linejoin="round"/>')
out.append('  <!-- The palette: white strokes, 0.375 px x 1.284^i wide. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="#FFFFFF" '
               f'stroke-width="{num(w)}"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
