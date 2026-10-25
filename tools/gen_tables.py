#!/usr/bin/env python3
"""Generates machines/rings/tables.h: every constant and lookup table the Q31 engine uses, derived from the float
reference (port-kit/engine/rings.c) with its exact numbers. Run from the repository root:

    python3 tools/gen_tables.py

The machine folder may hold only source, so this generator lives here. It also checks that the state-variable
filters the engine uses have the same response as the reference's RBJ biquads.
"""
import math
from pathlib import Path

import numpy as np

FS = 44100.0
f32 = lambda x: float(np.float32(x))

# ---- the reference's constants (rings.c) ----
ENV2, FB2 = f32(44.7054901), f32(8.09626961)
PRESENCE, BITE, HIGHSHELF = f32(0.858333349), f32(0.600000024), f32(7.69999981)
SWEEPC, RDECAY, RTONE, RSHAPE = f32(0.640722632), f32(0.778124988), f32(0.837460935), f32(0.629902363)
RSTRIKE, MSPREAD, WET = f32(0.333730459), f32(0.00999999978), f32(0.650000036)
DROP_SPLIT = 0.53306640625
SW_FROM, SW_TO, SW_CURVE, SW_CURVE2, SW_MID = 0.6274609374999999, 0.060000000000000005, 0.641015625, 0.10890624999999987, 0.47291015625
TM_FROM, TM_TO, TM_CURVE, TM_CURVE2, TM_MID = 0.49197265625000003, 0.6733984375000001, 0.6329296875, 0.6794140624999999, 0.66265625
DIST_PAD, G_FM, G_FX = f32(0.0853168), f32(-1.5483), f32(-0.7249)
HP_RATIO, HP_Q, LP_FC, LP_Q, HS_RATIO = 0.45, 0.7, 20708.0, 0.2, 6.0
ATTACK_MIN, RES_GAIN, STRIKE_P0 = 0.003, 2.66, 0.012
BLOCK = 16
MODAL_TAB = [[1, 2, 3, 4, 5, 6, 7, 8],
             [1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81, 31.87],
             [0.5, 1, 1.2, 1.5, 2, 2.5, 3, 4],
             [1, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501]]
MODE_HASH = [0.31, -0.83, 0.62, -0.17, 0.94, -0.55, 0.08, -0.71]

# ---- the machine's scaling (see engine.cpp) ----
# Output gain: the brief's gain 143, so the loudest reference setting (ratio 1) peaks near 0x10000000.
OUT_GAIN = 143.0
CORE_SCALE = 0.5     # the FM core runs at half the reference's level (carrier at 1/16 full scale)
DRIVE_SCALE = 8.0    # into and through the resonator: 8x the reference's level
LOGQ = 24            # log2 values are Q24
MODE_SHIFT = 2       # the mode states carry 2^MODE_SHIFT x their share of the mix, for precision

def q(x, bits=31):
    v = int(round(x * (1 << bits)))
    assert -(1 << 31) <= v <= (1 << 31) - 1, (x, bits)
    return v

def qs(x, bits=31):  # saturating: 1.0 in Q31 becomes the maximum
    return max(-(1 << 31), min((1 << 31) - 1, int(round(x * (1 << bits)))))

def log2q(x):
    return q(math.log2(x), LOGQ)

out = []
def const(name, value, comment):
    out.append(f'const i32 {name} = {value};{" " * max(1, 34 - len(name) - len(str(value)))}// {comment}')

def table(name, values, ctype='i32', per=8, comment=''):
    if comment:
        out.append('// ' + comment)
    out.append(f'const {ctype} {name}[{len(values)}] = {{')
    for i in range(0, len(values), per):
        out.append('    ' + ', '.join(str(v) for v in values[i:i + per]) + ',')
    out.append('};')

# ---- lookup tables ----
SINE = [max(-(1 << 30) + 1, min((1 << 30) - 1, q(math.sin(2 * math.pi * i / 2048), 30))) for i in range(2049)]
LOG2T = [q(math.log2(1 + i / 1024), 30) for i in range(1025)]
EXP2T = [int(round(2 ** (i / 1024) * (1 << 30))) for i in range(1025)]
SP2T = [q(math.log2(1 + 2 ** -(i / 32)), 30) for i in range(1025)]   # log2(1 + 2^-a), a = 0 .. 32

# ---- FM core ----
fb = 0.5 * FB2 / 128.0
depth0 = min(1.0, 4 * (ENV2 / 128) ** 2)
hb = []
def hiir(nbr, tbw):
    k = math.tan((1 - 2 * tbw) * math.pi / 4) ** 2
    kk = (1 - k * k) ** 0.25
    e = 0.5 * (1 - kk) / (1 + kk); e4 = e ** 4
    qq = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))
    order = 2 * nbr + 1
    res = []
    for idx in range(nbr):
        c = idx + 1
        num = den = 0.0
        i, j = 0, 1
        while True:
            t = j * qq ** (i * (i + 1)) * math.sin((2 * i + 1) * c * math.pi / order); num += t; j = -j; i += 1
            if abs(t) <= 1e-100: break
        i, j = 1, -1
        while True:
            t = j * qq ** (i * i) * math.cos(2 * i * c * math.pi / order); den += t; j = -j; i += 1
            if abs(t) <= 1e-100: break
        ww = num * qq ** 0.25 / (den + 0.5); w2 = ww * ww
        x = math.sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2)
        res.append(f32((1 - x) / (1 + x)))
    return res
hb = hiir(4, 0.068)

# ---- the onset (the reference's shortest attack, 3 ms, eased) per 16-sample sub-block ----
dt = BLOCK / FS
att_step = dt / ATTACK_MIN
ease = []
for j in range(1, 40):
    w = j * dt / ATTACK_MIN
    ease.append(1.0 if w >= 1 else w * w * (3 - 2 * w))
    if w >= 1: break

# ---- stage gains (signs dropped: both stages are negative, so the product is unchanged) ----
gs1 = abs(DIST_PAD * G_FM)
gs2 = abs(DIST_PAD * G_FX) * OUT_GAIN
# drive = DRIVE_SCALE * gs1 * x_ref, x_ref = 0.25 * (s0 + s1) / CORE_SCALE
DRIVE_GAIN = DRIVE_SCALE * gs1 * 0.25 / CORE_SCALE
G2 = gs2 / DRIVE_SCALE                  # mix (drive units) to output
DRY = (1 - WET) * G2
WETG = WET * G2

# ---- filters: Simper's trapezoidal SVF, equal to RBJ ----
def svf(kind, f, Q, dbgain=0.0):
    A = 10 ** (dbgain / 40)
    w = math.pi * f / FS
    s, c = math.sin(w), math.cos(w)
    if kind == 'hs':
        k = 1 / Q; ga = math.sqrt(A)
        D = c * c + A * s * s + ga * s * c * k
        a1, a2, a3 = c * c / D, ga * s * c / D, A * s * s / D
        m = (A * A, k * (1 - A) * A, 1 - A * A)
    else:
        k = 1 / (Q * A) if kind == 'pk' else 1 / Q
        D = c * c + s * s + s * c * k
        a1, a2, a3 = c * c / D, s * c / D, s * s / D
        m = {'lp': (0, 0, 1), 'hp': (1, -k, -1), 'pk': (1, k * (A * A - 1), 0)}[kind]
    return (a1, a2, a3), m

def svf_response(coef, m, w):
    (a1, a2, a3), (m0, m1, m2) = coef, m
    # run the SVF on an impulse and take the FFT
    n = 1 << 15
    x = np.zeros(n); x[0] = 1
    y = np.zeros(n); ic1 = ic2 = 0.0
    for i in range(4096):
        v0 = x[i]; v3 = v0 - ic2
        v1 = a1 * ic1 + a2 * v3; v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2
        y[i] = m0 * v0 + m1 * v1 + m2 * v2
    return np.fft.rfft(y)

def rbj(kind, f, Q, dbgain=0.0):
    w0 = 2 * math.pi * f / FS; al = math.sin(w0) / (2 * Q); c = math.cos(w0); A = 10 ** (dbgain / 40)
    if kind == 'hp': b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == 'lp': b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == 'pk': b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    else:
        sn = math.sin(w0); al = sn / 2 * math.sqrt(2); sq = 2 * math.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
        a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    n = 1 << 15
    z = np.exp(-1j * np.pi * np.arange(n // 2 + 1) / (n // 2))
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)

for kind, f, Q, g in [('hp', 0.45 * 130.8, HP_Q, 0), ('lp', LP_FC, LP_Q, 0), ('pk', 4 * 261.6, 0.8, 12 * PRESENCE),
                      ('hs', 6 * 261.6, 1 / math.sqrt(2), HIGHSHELF), ('hs', 0.45 * FS, 1 / math.sqrt(2), HIGHSHELF)]:
    coef, m = svf(kind, f, Q, g)
    err = np.max(np.abs(20 * np.log10(np.abs(svf_response(coef, m, None)[1:-200]) / np.abs(rbj(kind, f, Q, g)[1:-200]))))
    assert err < 0.01, (kind, f, err)

lp_coef, _ = svf('lp', LP_FC, LP_Q)
PK_A = 10 ** (12 * PRESENCE / 40); PK_K = 1 / (0.8 * PK_A)
HS_A = 10 ** (HIGHSHELF / 40); HS_K = math.sqrt(2)

# ---- modes ----
s = RSHAPE * 3; mi = min(2, int(s)); u = s - mi
qk = 1.5 - 1.3 * RTONE; pa = 1.2 * (1 - RTONE)
T = 0.1 * 100 ** RDECAY
rm = [2 ** (math.log2(MODAL_TAB[mi][k]) * (1 - u) + math.log2(MODAL_TAB[mi + 1][k]) * u) for k in range(8)]
ra = [max(1, r) ** -pa for r in rm]
rt = [max(1, r) ** -qk for r in rm]
pole = [math.exp(-6.9078 / (T * rt[k] * FS)) for k in range(8)]
bw = [6.9078 / (math.pi * T * rt[k]) + 1 for k in range(8)]
spread = [MSPREAD * 50 * MODE_HASH[k] / 1200 for k in range(8)]
Wp = 0.001 * 0.2 ** RTONE
sa = STRIKE_P0 * RSTRIKE
pulse = []
n = 0
while n / FS < Wp:
    t = n / FS
    pulse.append(-DRIVE_SCALE * sa * 0.5 * (1 - math.cos(2 * math.pi * t / Wp)))
    n += 1
# Mode inputs: the drive into the modes carries the dry share (DRY), and each mode's state 2^MODE_SHIFT x its share.
MS = 2 ** MODE_SHIFT
# b = (1 - r) M ra RES_GAIN x ..., M = 2 sqrt(r) sin th sqrt(1 + eps): all but sin th and sqrt(1 + eps) here
lb = [log2q((1 - pole[k]) * 2 * math.sqrt(pole[k]) * ra[k] * RES_GAIN * WETG * MS / DRY) for k in range(8)]
leps = [log2q((1 - pole[k]) ** 2 / (4 * pole[k])) for k in range(8)]
# strike: bi = sin th sqrt(ra) RES_GAIN edge wk 2 / (Wp fs wsum); the pulse is in drive units before the dry share
lbp = [log2q(RES_GAIN * 2 / (Wp * FS) * WETG * MS) for k in range(8)]
# ---- DROP ----
def sw_st(v):
    st = math.floor((v - 0.5) * 72 * 2 + 0.5) / 2
    return 0.0 if abs(st) < 0.5 else st
curve_exp = lambda c: 4 ** (-(c - 0.5) * 2)
sw_a0, sw_a1 = sw_st(SW_FROM), sw_st(SW_TO)
t0 = 5 * 60 ** TM_FROM; t1 = 5 * 60 ** TM_TO        # ms
sweep_e = 1 + 3 * (SWEEPC - 0.5) * 2
assert SWEEPC >= 0.5

# The second fixed low-pass now runs ahead of the modes, so STRIKE, which went through it after the modes, goes
# through it here instead: a fixed filter on a fixed pulse.
lp_b, lp_a = None, None
def rbj_lp(f, Q):
    w0 = 2 * math.pi * f / FS; al = math.sin(w0) / (2 * Q); c = math.cos(w0); a0 = 1 + al
    return [(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0], [1, -2 * c / a0, (1 - al) / a0]
lp_b, lp_a = rbj_lp(LP_FC, LP_Q)
padded = pulse + [0.0] * 60
strike = []
x1 = x2 = y1 = y2 = 0.0
for x in padded:
    y = lp_b[0] * x + lp_b[1] * x1 + lp_b[2] * x2 - lp_a[1] * y1 - lp_a[2] * y2
    x2, x1, y2, y1 = x1, x, y1, y
    strike.append(y)
while abs(strike[-1]) < 1e-6 * max(abs(v) for v in strike): strike.pop()
strike = strike[:48]
assert len(strike) <= 64

# ---- write ----
out.append('// Generated by tools/gen_tables.py from the float reference engine. Do not edit.')
out.append('#pragma once')
out.append('#include <opxy/types.hpp>')
out.append('namespace rings {')
out.append('using opxy::i32;')
out.append('using opxy::u32;')
out.append('')
const('LOGQ', LOGQ, 'log2 values are Q24')
const('FB_PHASE', q(4 * fb), "op2 feedback: op2's last output (Q30) times this is its phase offset (2^32 per cycle)")
const('DEPTH0', q(depth0, 29), 'FM depth, Q29')
const('BITE_AMOUNT', q(3 * BITE, 29), 'BITE: extra depth at note-on, Q29')
const('BITE_DECAY', q(math.exp(-dt / 0.025)), 'BITE falls by this per 16 samples, Q31')
table('HALFBAND', [q(c) for c in hb], comment='2x to 1x polyphase half-band allpass coefficients, Q31')
const('ATTACK_STEP', q(att_step), "the onset's linear level ramp per 16 samples (3 ms), Q31")
table('EASE', [qs(w) for w in ease], comment="the onset's ease-in (smoothstep) after each 16 samples, Q31")
const('MODE_SHIFT', MODE_SHIFT, 'the mode states carry 2^this x their share of the mix')
const('HP1_GAIN', q(DRIVE_GAIN * DRY), 'core (s0 + s1) to the drive, times the dry share: folded into the first high-pass, Q31')
const('LP_B0', q(lp_b[0]), 'fixed 20.7 kHz low-pass, Q 0.2, direct form: b0 (b1 = 2 b0, b2 = b0), Q31')
const('LP_A1', q(lp_a[1], 30), 'a1, Q30')
const('LP_A2', q(lp_a[2]), 'a2, Q31')
const('HP_RATIO', q(HP_RATIO), 'keytracked high-pass at 0.45 x the note, Q31')
const('HP_INVQ', q(1 / HP_Q, 30), 'keytracked high-pass 1/Q, Q30')
const('PK_INVQ', q(1 / 0.8, 30), 'presence peak 1/Q, Q30')
const('PK_A', q(PK_A, 29), 'presence peak A (+10.3 dB), Q29')
const('PK_AINV', q(1 / PK_A), '1/A, Q31')
const('HS_A', q(HS_A, 29), 'high shelf A (+7.7 dB), Q29')
const('HS_AP1', q(HS_A + 1, 28), 'A + 1, Q28')
const('HS_AM1', q(HS_A - 1, 28), 'A - 1, Q28')
const('HS_SQ', q(2 * math.sqrt(2 * HS_A), 28), '2 sqrt(2 A): with sin(w0/2) cos(w0/2), 2 sqrt(A) alpha (slope 1), Q28')
const('INC_PRESENCE_RATIO', 4, 'presence peak at 4x the note')
const('INC_SHELF_RATIO', 6, 'high shelf at 6x the note')
const('INC_LIMIT', int(0.45 * 2 ** 32), '0.45 fs as a phase increment')
const('INC_SIXTH', int(2 ** 32 / 7), 'modes below fs/7 keep 2 - 2 r cos th as a Q31 coefficient')
const('INC_2X_LIMIT', int(0.2 * 2 ** 32), 'the drop keeps the 2x carrier below 0.2 of its rate')
const('INC_20HZ', int(round(20 / FS * 2 ** 32)), '20 Hz as a phase increment')
const('L_INC_20HZ', log2q(20 / FS * 2 ** 32), 'its log2')
const('L_INC_NY', log2q(0.45 * 2 ** 32), 'log2 of 0.45 fs as an increment')
const('L_INC_NY09', log2q(0.9 * 0.45 * 2 ** 32), 'log2 of 0.9 x 0.45 fs')
const('L_INC_NY01', log2q(0.1 * 0.45 * 2 ** 32), 'log2 of 0.1 x 0.45 fs')
const('L_FS', log2q(FS), 'log2 44100')
const('RATIO_MIN', q(0.05), 'make-up: below this RATIO the lattice is the note alone')
const('UP_POWER', q(0.75), 'make-up for modes moved up: (ratio)^0.75')
const('STRETCH_DEAD', q(0.01), 'STRETCH within this of the middle is none')
const('DEPTH_BITE', q(depth0 * 3 * BITE, 29), 'FM depth added by BITE at note-on, Q29')
const('FLUSH', 4096, 'an unfed mode quieter than this (-126 dBFS at the output) is silent')
const('KK_MAKEUP', q(1 - math.exp(-64 / (FS * 0.02))), "make-up gain easing per 64 samples (the reference's 20 ms)")
table('L_RM', [log2q(r) for r in rm], comment="log2 of each mode's material ratio, Q24")
table('RM_DIST', [q(abs(r - 1), 26) for r in rm], comment='|material ratio - 1|, Q26')
table('L_SPREAD', [q(x, LOGQ) for x in spread], comment="each mode's fixed detune, log2, Q24")
table('L_BW', [log2q(b) for b in bw], comment="log2 of each mode's bandwidth in Hz")
table('TWO_1MR', [q(2 * (1 - p)) for p in pole], comment="each mode's 2 (1 - r), r its pole radius (ring 3.6 s x rt), Q31")
table('POLE', [q(p) for p in pole], comment='r, Q31')
table('ETA', [q(1 - p * p) for p in pole], comment='1 - r^2, Q31')
table('L_B', lb, comment='log2 of (1 - r) 2 sqrt(r) ra RES_GAIN x wet / dry x 2^MODE_SHIFT: the input gain before sin th, sqrt(1 + eps), make-up, edge and MODES')
table('L_EPS', leps, comment='log2 of (1 - r)^2 / (4 r): with sin(th)^2, the correction sqrt(1 + eps)')
table('L_BP', lbp, comment='log2 of RES_GAIN 2 / (Wp fs) x wet x 2^MODE_SHIFT: STRIKE into each mode, before sin th and its share sqrt(ra) edge MODES / wsum')
table('SQRT_RA', [qs(math.sqrt(r)) for r in ra], comment='sqrt(ra), Q31: shares the strike between modes')
table('STRIKE', [q(v) for v in strike], comment='the STRIKE pulse (raised cosine, 0.26 ms) through the fixed low-pass, at the drive level before the dry share, Q31; negative, as the drive carries the reference stage gain with its sign dropped')
const('DROP_SPLIT_INV', q(1 / DROP_SPLIT, 29), '1 / DROP_SPLIT, Q29')
const('DROP_SPLIT_Q31', q(DROP_SPLIT), 'DROP_SPLIT, Q31')
const('DROP_HI_INV', q(1 / (1 - DROP_SPLIT), 29), '1 / (1 - DROP_SPLIT), Q29')
const('SW_A0', q(sw_a0 / 12, LOGQ), "the sweep's start lane end, octaves, Q24")
const('SW_SPAN', q((sw_a1 - sw_a0) / 12, LOGQ), 'the sweep lane, end minus start, octaves, Q24')
const('SW_MID', q(SW_MID), 'sweep lane split point, Q31')
const('SW_E1', q(curve_exp(SW_CURVE), 29), 'sweep lane curve below the split, Q29')
const('SW_E2', q(curve_exp(SW_CURVE2), 29), 'above it, Q29')
const('SW_DEAD', q(0.05 / 12, LOGQ), 'sweeps under 0.05 semitones are none')
const('TM_MID', q(TM_MID), 'time lane split point, Q31')
const('TM_E1', q(curve_exp(TM_CURVE), 29), 'time lane curve below the split, Q29')
const('TM_E2', q(curve_exp(TM_CURVE2), 29), 'above it, Q29')
const('L_DT_T0', log2q(dt * 1000 / t0), "log2 of 16 samples over the time lane's start (37 ms)")
const('L_T1_T0', log2q(t1 / t0), "log2 of the time lane's end over its start")
const('SWEEP_E', q(sweep_e - 1, 31), "DROP's shape: what is left is 1 - u^(1 + this), Q31")
table('SINE', SINE, per=8, comment='sin, 2048 points per cycle and a guard point, Q30')
table('LOG2T', LOG2T, comment='log2(1 + i/1024), Q30')
table('EXP2T', EXP2T, ctype='u32', comment='2^(i/1024), Q30')
table('SP2T', SP2T, comment='log2(1 + 2^-a), a = i/32, Q30')
out.append('}')

Path('machines/rings/tables.h').write_text('\n'.join(out) + '\n')
print('machines/rings/tables.h written')
print('modes: rm', [round(r, 4) for r in rm])
print('       pole 1-r', [f'{1 - p:.3g}' for p in pole], 'strike samples', len(pulse))
print(f'gains: drive {DRIVE_GAIN:.5f}  G2 {G2:.5f}  dry {DRY:.5f}  wet {WETG:.5f}')
