#!/usr/bin/env python3
"""Renders every setting of port-kit/reference/manifest.json through the machine (its tests/sound.cpp, with an
output folder) and compares them with port-kit/reference/compare.py, two ways:

  B        the port against the float reference as the machine runs it (ATTACK 0, SUSTAIN 1: on the device the
           stock amp envelope shapes the sound). Is the fixed-point port the same sound?
  vs kit   the port, with the stock amp envelope emulated (the setting's ATTACK, DECAY and SUSTAIN as one gain on
           the output), against the kit's original render. What does option B change?

    python3 tools/check_renders.py [--machine machines/rings2] [--out DIR] [--cxx g++]

Needs numpy. Run from the repository root, with the SDK checked out beside it (../open-xy).
"""
import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / 'port-kit'
SDK = ROOT.parent / 'open-xy'
FS = 44100
BLOCK = 16


def read_wav(path):
    sys.path.insert(0, str(KIT / 'reference'))
    import compare
    return compare.read_wav(str(path))[0]


def write_float_wav(path, x):
    x = np.asarray(x, '<f4')
    with open(path, 'wb') as f:
        f.write(b'RIFF' + (36 + 4 * len(x)).to_bytes(4, 'little') + b'WAVEfmt ')
        f.write((16).to_bytes(4, 'little') + (3).to_bytes(2, 'little') + (1).to_bytes(2, 'little'))
        f.write(FS.to_bytes(4, 'little') + (FS * 4).to_bytes(4, 'little') + (4).to_bytes(2, 'little') + (32).to_bytes(2, 'little'))
        f.write(b'data' + (4 * len(x)).to_bytes(4, 'little') + x.tobytes())


def stock_envelope(n, attack, decay, sustain):
    """The reference's ADSR as one gain, per 16 samples and interpolated, as an amp envelope applied after the
    voice: the level L the reference's two stages multiply to."""
    A = max(0.001, 0.001 * (4.0 / 0.001) ** attack)
    D = max(0.005, 0.05 * (20.0 / 0.05) ** decay)
    S = min(1.0, max(0.0, sustain * sustain))
    Ae = max(A, 0.003)
    dt = BLOCK / FS
    g, out, stage, L, u, t = 0.0, np.zeros(n), 1, 0.0, 0.0, 0.0
    prev = 0.0
    for b in range(n // BLOCK):
        if stage == 1:
            u = min(1.0, u + dt / Ae); t += dt
            w = t / 0.003; w = 1.0 if w >= 1 else w * w * (3 - 2 * w)
            gg = math.sqrt(u) * w
            if u >= 1 and w >= 1:
                L, stage = 1.0, 2
            else:
                L = gg * gg
        elif stage == 2:
            L *= 10 ** (-3 * dt / D)
            if L <= S:
                L, stage = S, 3
        else:
            L = S
        ramp = prev + (L - prev) * (np.arange(1, BLOCK + 1) / BLOCK)
        out[b * BLOCK:(b + 1) * BLOCK] = ramp
        prev = L
    return out


def compare(ref, port, gain_match=True):
    args = [sys.executable, str(KIT / 'reference/compare.py'), str(ref), str(port)] + (['--gain-match'] if gain_match else [])
    r = subprocess.run(args, capture_output=True, text=True)
    lines = {l.split()[0]: l for l in r.stdout.splitlines() if l.strip()}
    def num(key, pos):
        return float(lines[key].split()[pos])
    return {'level_med': num('level', 2), 'level_p99': num('level', 5), 'spectrum': num('spectrum', 3),
            'onset': num('onset', 3), 'pass': r.returncode == 0,
            'offset': lines['peak'].split('port was')[1].split('dB')[0].strip() if 'port was' in lines['peak'] else '0'}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--machine', default=str(ROOT / 'machines/rings2'))
    p.add_argument('--out', help='keep the renders here (default: a temporary folder)')
    p.add_argument('--cxx', default='g++')
    args = p.parse_args()
    machine = Path(args.machine).resolve()
    out = Path(args.out) if args.out else Path(tempfile.mkdtemp(prefix='rings-renders-'))
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'sound'
    subprocess.run([args.cxx, '-std=c++11', '-O2', '-I' + str(SDK / 'sdk/include'), '-I' + str(machine),
                    str(machine / 'tests/sound.cpp'), '-o', str(binary)], check=True)
    r = subprocess.run([str(binary), str(out)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr)
        print('(the sound test failed its own checks; comparing the renders anyway)\n')
    manifest = json.loads((KIT / 'reference/manifest.json').read_text())
    defaults = manifest['defaults']
    print(f'renders in {out}\n')
    print('                  B: port vs float reference, as the machine runs it   |  vs kit: port x stock envelope vs the kit render')
    print(f'{"setting":13s} {"level med/p99":>14s} {"spectrum":>9s} {"onset":>6s} {"":6s} | {"level med/p99":>14s} {"spectrum":>9s} {"onset":>6s}')
    all_b = True
    for item in manifest['renders']:
        name = Path(item['file']).stem
        knobs = dict(defaults, **item['knobs_changed'])
        b = compare(out / f'{name}.ref.wav', out / f'{name}.wav')
        all_b = all_b and b['pass']
        port = read_wav(out / f'{name}.wav')
        env = stock_envelope(len(port), knobs['attack'], knobs['decay'], knobs['sustain'])
        write_float_wav(out / f'{name}.env.wav', port * env)
        k = compare(KIT / 'reference' / item['file'], out / f'{name}.env.wav')
        print(f'{name:13s} {b["level_med"]:6.2f}/{b["level_p99"]:5.2f} dB {b["spectrum"]:6.2f} dB {b["onset"]:5.2f}  {"PASS" if b["pass"] else "CHECK"}'
              f'  | {k["level_med"]:6.2f}/{k["level_p99"]:5.2f} dB {k["spectrum"]:6.2f} dB {k["onset"]:5.2f}  {"PASS" if k["pass"] else "CHECK"}')
    print('\nB: ' + ('every setting PASSES compare.py: the port is the same sound' if all_b else 'some settings need a listen'))
    return 0 if all_b else 2


if __name__ == '__main__':
    sys.exit(main())
