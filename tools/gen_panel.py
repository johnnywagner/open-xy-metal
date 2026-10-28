#!/usr/bin/env python3
"""Generates machines/metal/panel.svg.fragment: the pieces display.cpp places, and its palette. Run from the
repository root:

    python3 tools/gen_panel.py

Twelve rings of 10 pieces each (node 10 r + i): four quarter arcs of radius 8, from the left round to the top
(display.cpp turns them for the other corners); four vertical pieces 64 px long; two 32 px long that display.cpp turns
across. Then the node markers, four a ring (node 120 + 4 r + i: top, bottom, left, right): 48 filled squares 16 px wide
(display.cpp turns them for diamonds), then 48 filled circles 16 px across (node 168 + 4 r + i). All start parked off
screen. The palette: twelve white strokes 0.375 px x 1.405^i wide (display.cpp gives each
piece the one that undoes its scale; WIDTHS there is this table in Q10), then the markers' fill. Every stroke is COLOUR: try others live in the
screen lab (tools/screen_lab.py), then set it here and rerun this.
"""
import math
from pathlib import Path

RINGS, PIECES, ARC, MARK = 12, 10, 8, 8
COLOUR = '#FFFFFF'                  # the rings' stroke and the markers' fill, #RRGGBB
# The straight pieces' line ends: 'square' as the screen had them, 'round' to try rounding the boxes the OP-XY draws
# (whether it draws line ends at all is not known; its boxes so far show flat ends, unextended).
ENDS = 'round'
WIDTHS = [0.375 * 1.405 ** i for i in range(12)]

def num(v):
    t = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if t in ('-0', '') else t

def path(pts):
    return 'M' + ' L'.join(f'{num(x)},{num(y)}' for x, y in pts)

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
            d, cap = 'M0,-32 L0,32', ENDS
        else:
            d, cap = 'M0,-16 L0,16', ENDS
        out.append(f'  <path id="metal_node_{n}" {PARK} d="{d}" fill="none" stroke="{COLOUR}" stroke-width="1.3" '
                   f'stroke-linecap="{cap}"/>')
out.append('  <!-- The node markers: four a ring, squares then circles. -->')
for shape in ('square', 'circle'):
    for n in range(RINGS * 4):
        node = RINGS * PIECES + (0 if shape == 'square' else RINGS * 4) + n
        art = (f'path id="metal_node_{node}" {PARK} d="M-{MARK},-{MARK} L{MARK},-{MARK} L{MARK},{MARK} L-{MARK},{MARK} Z"'
               if shape == 'square' else f'circle id="metal_node_{node}" {PARK} cx="0" cy="0" r="{MARK}"')
        out.append(f'  <{art} fill="{COLOUR}" stroke="{COLOUR}" stroke-width="1" stroke-linejoin="miter"/>')
out.append(f'  <!-- The palette: twelve {COLOUR} strokes, 0.375 px x 1.405^i wide. -->')
for i, w in enumerate(WIDTHS):
    out.append(f'  <circle id="metal_palette_{i}" {PARK} cx="0" cy="0" r="1" fill="#000000" stroke="{COLOUR}" '
               f'stroke-width="{w:.4f}"/>')
out.append(f'  <!-- 12: the markers, filled, with a 1 px edge. -->')
out.append(f'  <circle id="metal_palette_12" {PARK} cx="0" cy="0" r="1" fill="{COLOUR}" stroke="{COLOUR}" stroke-width="1"/>')
out.append('</g>')
Path('machines/metal/panel.svg.fragment').write_text('\n'.join(out) + '\n')
print('WIDTHS (Q10):', ', '.join(str(round(w * 1024)) for w in WIDTHS))
