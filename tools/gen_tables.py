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
# Tonal mappings (not in the reference): the modes stop at fs/4; STRETCH's middle is the harmonic series; RATIO is
# pulled onto simple op2 : carrier ratios.
MODE_TOP = 0.25
# With the first mode on the note and no lattice make-up, the modes catch more of the FM: their drive is trimmed.
RES_TRIM_DB = -8.0
RES_TRIM = 10 ** (RES_TRIM_DB / 20)
STRETCH_HARM = 0.06
MAGNETS = [1 / 8, 1 / 6, 1 / 5, 1 / 4, 1 / 3, 2 / 5, 1 / 2, 3 / 5, 2 / 3, 3 / 4, 1]
MAGNET_W = 0.07
ATTACK_MIN, RES_GAIN, STRIKE_P0 = 0.003, 2.66, 0.012
BLOCK = 16
MODAL_TAB = [[1, 2, 3, 4, 5, 6, 7, 8],
             [1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81, 31.87],
             [0.5, 1, 1.2, 1.5, 2, 2.5, 3, 4],
             [1, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501]]
MODE_HASH = [0.31, -0.83, 0.62, -0.17, 0.94, -0.55, 0.08, -0.71]

# ---- the machine's scaling (see engine.cpp) ----
# Output gain: the brief's gain 143 put the loudest reference setting (ratio 1) at 0x10000000, but rings then sat
# 13 dB below 2op FM (the SDK's stock-loudness reference) in K-weighted loudness. LEVEL_DB lifts it 12 dB; the engine
# clips at half scale, which only the loudest settings' first transient reaches.
LEVEL_DB = 12.0
OUT_GAIN = 143.0 * 10 ** (LEVEL_DB / 20)
CORE_X = 1.0         # the FM core's output is the reference's x (its carrier at 1/16 full scale; x = car / 2)
DRIVE_SCALE = 8.0    # into and through the resonator: 8x the reference's level
LOGQ = 24            # log2 values are Q24
MODE_SHIFT = 1       # the mode states carry 2^MODE_SHIFT x their share of the mix, for precision

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
# drive = DRIVE_SCALE * gs1 * x_ref, x_ref = core / CORE_X
DRIVE_GAIN = DRIVE_SCALE * gs1 / CORE_X
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
    pulse.append(-sa * 0.5 * (1 - math.cos(2 * math.pi * t / Wp)) / gs1)   # in drive units (the reference's / |gs1|)
    n += 1
# The drive (x_d) is the reference's resonator input without its stage gain; the mix is x_d + 2^-MODE_SHIFT sum(modes),
# so each mode carries wet / dry x 2^MODE_SHIFT of its reference level. Everything after is folded into the shelf.
MS = 2 ** MODE_SHIFT
WET_RATIO = WET / (1 - WET)
# b = (1 - r) M ra RES_GAIN x ..., M = 2 sqrt(r) sin th sqrt(1 + eps): all but sin th and sqrt(1 + eps) here
lb = [log2q((1 - pole[k]) * 2 * math.sqrt(pole[k]) * ra[k] * RES_GAIN * WET_RATIO * MS * RES_TRIM) for k in range(8)]
leps = [log2q((1 - pole[k]) ** 2 / (4 * pole[k])) for k in range(8)]
# strike: bi = sin th sqrt(ra) RES_GAIN edge wk 2 / (Wp fs wsum)
lbp = [log2q(RES_GAIN * 2 / (Wp * FS) * WET_RATIO * MS) for k in range(8)]
# the mix to the output: the dry share, both stage gains and the output gain (143)
OUT_G = (1 - WET) * gs1 * gs2
# ---- DROP ----
def sw_st(v):
    st = math.floor((v - 0.5) * 72 * 2 + 0.5) / 2
    return 0.0 if abs(st) < 0.5 else st
curve_exp = lambda c: 4 ** (-(c - 0.5) * 2)
sw_a0, sw_a1 = sw_st(SW_FROM), sw_st(SW_TO)
t0 = 5 * 60 ** TM_FROM; t1 = 5 * 60 ** TM_TO        # ms
sweep_e = 1 + 3 * (SWEEPC - 0.5) * 2
assert SWEEPC >= 0.5

# STRIKE goes into the modes as the reference's raw pulse: every filter now runs after the modes, on the mix.
def rbj_lp(f, Q):
    w0 = 2 * math.pi * f / FS; al = math.sin(w0) / (2 * Q); c = math.cos(w0); a0 = 1 + al
    return [(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0], [1, -2 * c / a0, (1 - al) / a0]
lp_b, lp_a = rbj_lp(LP_FC, LP_Q)
strike = pulse + [0.0] * (16 - len(pulse))      # padded to the 16-sample step the strike runs in

# The presence peak (4x the note, Q 0.8, +10.3 dB) and the high shelf (6x the note, slope 1, +7.7 dB) as one biquad:
# an analog prototype in s / (2 pi f0), fitted to their product (scipy least squares on log magnitude, 0.05..60 x the
# note), made digital per note by the bilinear transform warped at the note. Within 0.26 dB to 25 x the note.
EQ = dict(b2=2.5799, b1=11.3473, b0=16.21, a1=3.4644, a0=16.1646)
def eq_target(w):
    A1 = 10 ** (12 * PRESENCE / 40); A2 = 10 ** (HIGHSHELF / 40); sj = 1j * w
    x = sj / 4; pk = (x * x + x * A1 / 0.8 + 1) / (x * x + x / (A1 * 0.8) + 1)
    x = sj / 6; Q = 1 / math.sqrt(2); hs = A2 * (A2 * x * x + math.sqrt(A2) / Q * x + 1) / (x * x + math.sqrt(A2) / Q * x + A2)
    return pk * hs
_w = np.logspace(math.log10(0.05), math.log10(25), 400); _s = 1j * _w
_fit = (EQ['b2'] * _s * _s + EQ['b1'] * _s + EQ['b0']) / (_s * _s + EQ['a1'] * _s + EQ['a0'])
assert np.max(np.abs(20 * np.log10(np.abs(_fit) / np.abs(eq_target(_w))))) < 0.3

# The whole keytracked chain after the modes, both high-passes (0.45 x the note, Q 0.7) and that EQ, sixth order, as two
# biquads (fourth order): a high-pass s^2 / (s^2 + a1 s + a0) and a band s (b2 s + b1) / (s^2 + c1 s + c0), fitted to the
# product on log magnitude, 0.1..25 x the note (below 0.3 weighted 0.3). Within 1.4 dB from 0.2 x the note (where the
# chain is already 28 dB down), 0.9 dB from half the note, 0.7 dB above 4x; 4 dB at 0.1..0.2 (30 to 52 dB down).
CHAIN = dict(a1=0.3066, a0=0.21686, b2=2.43331, b1=16.70592, c1=5.56414, c0=20.15355)
def hp_analog(r):
    W = 1j * r / HP_RATIO
    return W * W / (W * W + W / HP_Q + 1)
def eq_fit(r):
    s_ = 1j * r
    return (EQ['b2'] * s_ * s_ + EQ['b1'] * s_ + EQ['b0']) / (s_ * s_ + EQ['a1'] * s_ + EQ['a0'])
def chain_hp(r):
    s_ = 1j * r
    return s_ * s_ / (s_ * s_ + CHAIN['a1'] * s_ + CHAIN['a0'])
def chain_band(r):
    s_ = 1j * r
    return (CHAIN['b2'] * s_ * s_ + CHAIN['b1'] * s_) / (s_ * s_ + CHAIN['c1'] * s_ + CHAIN['c0'])
def chain_fit(r):
    return chain_hp(r) * chain_band(r)
_e = 20 * np.log10(np.abs(chain_fit(_w)) / np.abs(hp_analog(_w) ** 2 * eq_fit(_w)))
assert np.max(np.abs(_e[_w >= 0.2])) < 1.4 and np.max(np.abs(_e[_w >= 0.5])) < 0.95

# In the reference STRIKE skips the first high-pass and the presence peak (they shape the drive) and goes through the
# second high-pass and the shelf. Here the fitted high-pass shapes the drive and the band follows the modes, so each
# mode's strike is rotated by G = HP HS / BAND = HP EQ / (PK BAND) at the mode, r its ratio to the note (analog
# prototypes), magnitude and phase (engine.cpp, strike_step).
def hp_pk(r):
    W = 1j * r / HP_RATIO
    hp = W * W / (W * W + W / HP_Q + 1)
    A1 = 10 ** (12 * PRESENCE / 40); x = 1j * r / 4
    pk = (x * x + x * A1 / 0.8 + 1) / (x * x + x / (A1 * 0.8) + 1)
    return hp * pk
def strike_gain(r):
    return hp_analog(r) * eq_fit(r) / (hp_pk(r) / hp_analog(r) * chain_band(r))
strike_g = [strike_gain(2 ** (i / 16 - 4)) for i in range(161)]

# Both fixed low-passes as one 3-tap linear-phase FIR with DC gain 1, x1 + h (x0 + x2 - 2 x1): |H|^2 of the
# low-pass, least squares to 16 kHz (within 0.7 dB to 8 kHz, 0.9 dB to 12 kHz, 1.3 dB to 16 kHz).
fr = np.linspace(10, 20000, 3000)
zz = np.exp(-1j * 2 * np.pi * fr / FS)
target = np.abs((lp_b[0] + lp_b[1] * zz + lp_b[2] * zz * zz) / (lp_a[0] + lp_a[1] * zz + lp_a[2] * zz * zz)) ** 2
basis = np.stack([np.ones_like(fr), 2 * np.cos(2 * np.pi * fr / FS)], 1)
weight = np.where(fr < 16000, 1.0, 0.2)
fir = np.linalg.lstsq(basis * weight[:, None], target * weight, rcond=None)[0]
fir = fir / (fir[0] + 2 * fir[1])
# h rounded to 1/8 + 1/512, so the engine applies it with two shifts
assert abs(fir[1] - (1 / 8 + 1 / 512)) < 5e-4, fir[1]
fir = np.array([1 - 2 * (1 / 8 + 1 / 512), 1 / 8 + 1 / 512])
fir_err = 20 * np.log10(np.abs(basis @ fir)[fr < 16000] / target[fr < 16000])
assert np.max(np.abs(fir_err)) < 1.4, np.max(np.abs(fir_err))

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
const('ATTACK_STEP', q(att_step), "the onset's linear level ramp per 16 samples (3 ms), Q31")
table('EASE', [qs(w) for w in ease], comment="the onset's ease-in (smoothstep) after each 16 samples, Q31")
const('MODE_SHIFT', MODE_SHIFT, 'the mode states carry 2^this x their share of the mix')
const('LEVEL_DB', int(LEVEL_DB), 'the output level over the brief (dB)')
const('LP_FIR', q(fir[1]), 'both fixed 20.7 kHz low-passes (Q 0.2) as one 3-tap linear-phase FIR, DC gain 1: x1 + h (x0 + x2 - 2 x1), h = 1/8 + 1/512, Q31')
const('L_OUT', log2q(OUT_G), 'log2 of the mix to the output: dry share x both stage gains x 143 x LEVEL_DB; folded into the EQ')
const('HP_RATIO', q(HP_RATIO), 'keytracked high-pass at 0.45 x the note, Q31')
const('L_CH_A1', log2q(CHAIN['a1']), 'the chain after the modes, analog prototype in s / (2 pi f0): high-pass, log2 a1')
const('L_CH_A0', log2q(CHAIN['a0']), 'log2 a0')
const('L_CH_B2', log2q(CHAIN['b2']), 'band s (b2 s + b1) / (s^2 + c1 s + c0): log2 b2')
const('L_CH_B1', log2q(CHAIN['b1']), 'log2 b1')
const('L_CH_C1', log2q(CHAIN['c1']), 'log2 c1')
const('L_CH_C0', log2q(CHAIN['c0']), 'log2 c0')
const('HALF_SCALE', (1 << 30) - 1, 'the output ceiling: half scale, the SDK limit')
const('SINE_CURVE', q((2 * math.pi / 2048) ** 2 / 2), "sine(): the table's curvature, (2 pi / 2048)^2 / 2, Q31")
const('INC_LIMIT', int(MODE_TOP * 2 ** 32), 'the modes stop at fs/4 (11 kHz) as a phase increment')
const('INC_SIXTH', int(2 ** 32 / 7), 'modes below fs/7 keep 2 - 2 r cos th as a Q31 coefficient')
const('INC_EIGHTH', int(2 ** 32 / 8), 'modes below fs/8 take their input once per four samples')
const('OP2_HALF_RATE', int(round(260 / FS * 2 ** 32)), 'op2 runs at half rate while slower than this (260 Hz) as a 1x increment')
const('INC_DROP_LIMIT', int(0.4 * 2 ** 32), 'the drop keeps the carrier below 0.4 fs (0.2 of the reference 2x rate)')
const('INC_20HZ', int(round(20 / FS * 2 ** 32)), '20 Hz as a phase increment')
const('L_INC_20HZ', log2q(20 / FS * 2 ** 32), 'its log2')
const('L_INC_NY', log2q(MODE_TOP * 2 ** 32), 'log2 of the top (fs/4) as an increment')
const('L_INC_NY09', log2q(0.9 * MODE_TOP * 2 ** 32), 'log2 of 0.9 x the top: modes fade out above')
const('L_INC_NY01', log2q(0.1 * MODE_TOP * 2 ** 32), 'log2 of 0.1 x the top')
const('L_FS', log2q(FS), 'log2 44100')
const('UP_POWER', q(0.75), 'make-up for modes moved up: (ratio)^0.75')
const('RES_TRIM', q(RES_TRIM), 'the modes\' drive trim (RES_TRIM_DB), Q31: for the tests')
const('STRETCH_HARM', q(STRETCH_HARM), "STRETCH within this of the middle (as 2 x - 1) is the harmonic series")
const('STRETCH_MORPH_INV', q(1 / (0.5 - STRETCH_HARM), 29), 'harmonic to material over the rest of the first half of each side, 1 / its width, Q29')
table('L_MAGNET', [log2q(m) for m in MAGNETS], comment="RATIO's magnets (op2 : carrier), log2, Q24")
const('MAGNET_W', q(MAGNET_W, LOGQ), "RATIO is pulled onto a magnet within this many octaves of it, Q24")
const('MAGNET_INV', int(2 ** 31 / (MAGNET_W * 2 ** LOGQ)), "2^31 / MAGNET_W in Q24 units: d / W as Q31")
const('DEPTH_BITE', q(depth0 * 3 * BITE, 29), 'FM depth added by BITE at note-on, Q29')
const('FLUSH', 4096, 'an unfed mode quieter than this (-126 dBFS at the output) is silent')
const('KK_MAKEUP', q(1 - math.exp(-64 / (FS * 0.02))), "make-up gain easing per 64 samples (the reference's 20 ms)")
table('L_RM', [log2q(r) for r in rm], comment="log2 of each mode's material ratio, Q24")
table('L_H', [log2q(k + 1) for k in range(8)], comment="log2 of the harmonic series, mode k at k + 1 times the note, Q24")
table('L_RA', [log2q(r / rm[0]) for r in rm], comment="log2 of each mode's material ratio over the first mode's (the first on the note), Q24")
table('L_SPREAD', [q(x, LOGQ) for x in spread], comment="each mode's fixed detune, log2, Q24")
table('L_BW', [log2q(b) for b in bw], comment="log2 of each mode's bandwidth in Hz")
table('DAMP16', [q(p ** 16) for p in pole], comment="each mode's decay over 16 samples, r^16 (ring 3.6 s x rt), Q31")
table('L_B', lb, comment='log2 of (1 - r) 2 sqrt(r) ra RES_GAIN x wet / dry x 2^MODE_SHIFT: the input gain before sin th, sqrt(1 + eps), make-up, edge and MODES')
table('L_EPS', leps, comment='log2 of (1 - r)^2 / (4 r): with sin(th)^2, the correction sqrt(1 + eps)')
table('L_BP', lbp, comment='log2 of RES_GAIN 2 / (Wp fs) x wet x 2^MODE_SHIFT: STRIKE into each mode, before sin th and its share sqrt(ra) edge MODES / wsum')
table('SQRT_RA', [qs(math.sqrt(r)) for r in ra], comment='sqrt(ra), Q31: shares the strike between modes')
table('STRIKE', [q(v) for v in strike], comment='the STRIKE pulse (raised cosine, 0.26 ms), in the core\'s units, Q31; negative, as the core drops the reference stage gain and its sign')
assert max(max(abs(g.real), abs(g.imag)) for g in strike_g) < 3.9
table('STRIKE_GRE', [q(g.real, 29) for g in strike_g], comment='HP EQ / (PK BAND) at a mode, real part, by log2 of its ratio to the note from -4 in 1/16 octaves, Q29')
table('STRIKE_GIM', [q(g.imag, 29) for g in strike_g], comment='its imaginary part, Q29')
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
table('SINE16', [max(-32767, min(32767, int(round(math.sin(2 * math.pi * i / 4096) * 32768)))) for i in range(4096)], ctype='opxy::i16', per=16, comment='sin, 4096 points per cycle, Q15: the FM core reads it without interpolation (phase spurs below -70 dBc)')
table('LOG2T', LOG2T, comment='log2(1 + i/1024), Q30')
table('EXP2T', EXP2T, ctype='u32', comment='2^(i/1024), Q30')
table('SP2T', SP2T, comment='log2(1 + 2^-a), a = i/32, Q30')
out.append('}')

# rings' tables (machines/rings, removed; in git history) are the base every variant below starts from.

# ---- display tables (display_tables.h): screen only, Q10 ----
def lane_pos(x, mid, c1, c2):
    sp = DROP_SPLIT
    x = min(1.0, max(0.0, x))
    return mid * (x / sp) ** curve_exp(c1) if x <= sp else mid + (1 - mid) * ((x - sp) / (1 - sp)) ** curve_exp(c2)
d = ['// Generated by tools/gen_tables.py. Display only; never read by audio.', '#pragma once',
     '#include <opxy/types.hpp>', 'namespace rings {']
def dtable(name, values, comment):
    d.append('// ' + comment)
    d.append(f'const opxy::i32 {name}[{len(values)}] = {{')
    for i in range(0, len(values), 8):
        d.append('    ' + ', '.join(str(v) for v in values[i:i + 8]) + ',')
    d.append('};')
knobs = [i / 16 for i in range(17)]
st = []
for x in knobs:
    v = sw_a0 + (sw_a1 - sw_a0) * lane_pos(x, SW_MID, SW_CURVE, SW_CURVE2)
    st.append(round((0.0 if abs(v) < 0.05 else v) * 1024))
dtable('DISPLAY_DROP_ST', st, "DROP's start offset in semitones at the knob's 17 sixteenths, Q10")
dtable('DISPLAY_DROP_MS', [round(t0 * (t1 / t0) ** lane_pos(x, TM_MID, TM_CURVE, TM_CURVE2) * 1024) for x in knobs],
       "DROP's time in ms at the knob's sixteenths, Q10")
dtable('DISPLAY_DROP_LEFT', [round((1 - (i / 23) ** sweep_e) * 1024) for i in range(24)],
       "how much of the drop is left at 24 points through it (its fixed shape), Q10")
dtable('DISPLAY_L_H', [round(math.log2(k + 1) * 1024) for k in range(8)], 'log2 of the harmonic series, Q10')
dtable('DISPLAY_L_RA', [round(math.log2(r / rm[0]) * 1024) for r in rm], "log2 of each mode's material ratio over the first mode's, Q10")
dtable('DISPLAY_RING', [round(min(4.0, 0.9 * rt[k]) * 1024) for k in range(8)],
       "how long each mode's halo lasts on screen after a note, seconds Q10 (upper modes ring shorter)")
dtable('DISPLAY_RING_INV', [round(65536 / min(4.0, 0.9 * rt[k])) for k in range(8)], 'its reciprocal, Q16')
d.append('}')
print('modes: rm', [round(r, 4) for r in rm])
print('       pole 1-r', [f'{1 - p:.3g}' for p in pole], 'strike samples', len(pulse))
print(f'gains: drive {DRIVE_GAIN:.5f}  G2 {G2:.5f}  dry {DRY:.5f}  wet {WETG:.5f}')

# ======================================================================================================================
# rings2 and its variants: rings with the reference's own mode tuning (the material's ratios, STRETCH as 2^(2 x - 1)
# on them, RATIO free and the reference's lattice make-up). Each tables.h is rings' (in its own namespace) with its own
# drive trim, plus rings2's constants and the variant's own.
def write_variant(name, trim_db, note, extra=None):
    global out
    trim = 10 ** (trim_db / 20)
    saved, out = out, []
    table('L_B', [log2q((1 - pole[k]) * 2 * math.sqrt(pole[k]) * ra[k] * RES_GAIN * WET_RATIO * MS * trim) for k in range(8)],
          comment='log2 of (1 - r) 2 sqrt(r) ra RES_GAIN x wet / dry x 2^MODE_SHIFT: the input gain before sin th, sqrt(1 + eps), make-up, edge and MODES')
    lb = out; out = []
    const('RES_TRIM', q(trim, 29), f"the modes' drive trim ({trim_db:+.1f} dB: {note}), Q29: for the tests")
    tr = out; out = []
    out.append("// ---- rings2: the reference's mode tuning")
    const('RATIO_MIN', q(0.05), 'make-up: below this RATIO the lattice is the note alone')
    const('STRETCH_DEAD', q(0.01), 'STRETCH within this of the middle is none')
    table('RM_DIST', [q(abs(r - 1), 26) for r in rm], comment='|material ratio - 1|, Q26')
    if extra:
        extra()
    ex = out; out = saved
    body = list(out[:-1])
    i = body.index('const i32 L_B[8] = {'); body[i - 1:i + 3] = lb
    i = [j for j, line in enumerate(body) if line.startswith('const i32 RES_TRIM ')][0]; body[i:i + 1] = tr
    text = '\n'.join(body + ex + ['}']).replace('namespace rings {', 'namespace %s {' % name) + '\n'
    text = text.replace('// Generated by tools/gen_tables.py from the float reference engine. Do not edit.',
                        "// Generated by tools/gen_tables.py (rings' tables with this machine's trim, then its own). Do not edit.")
    Path(f'machines/{name}/tables.h').write_text(text)
    dd = d[:-1] + ["// log2 of each mode's material ratio, Q10", 'const opxy::i32 DISPLAY_L_RM[8] = {',
                   '    ' + ', '.join(str(round(math.log2(r) * 1024)) for r in rm) + ',', '};', '}']
    Path(f'machines/{name}/display_tables.h').write_text('\n'.join(dd).replace('namespace rings {', 'namespace %s {' % name) + '\n')
    print(f'machines/{name}/tables.h written')

# rings2: its modes catch less of the FM than rings' did (the first below the note), so it keeps the reference's drive.
write_variant('rings2', 0.0, 'none, the reference\'s drive')

# rings5 and rings6: rings2 tuned. STRETCH places the modes as rings2 does, then each mode with a pitch-class mask is
# moved to the nearest semitone of the note (12-TET, counted from the note) whose pitch class the mask allows; the
# small fixed detune goes on after. The masks: bit p allows p semitones above the note (or octaves of it).
CONSONANT = sum(1 << p for p in (0, 3, 4, 5, 7, 8, 9))   # unison/octaves, thirds, fourth, fifth, sixths
CHROMATIC = (1 << 12) - 1
def tune_tables(masks, what):
    def extra():
        out.append('// ---- ' + what)
        const('SEMI', int(round((1 << 24) / 12)), 'a semitone, log2, Q24')
        table('TUNE_MASK', masks, ctype='u32', comment="each mode's allowed pitch classes (bit p: p semitones above the note); 0: left where STRETCH puts it")
    return extra
# rings5, Tuned: the four lowest modes onto consonant intervals of the note, the four above left free (the metal).
write_variant('rings5', 0.0, 'none: level with rings2 at every STRETCH', tune_tables([CONSONANT] * 4 + [0] * 4, 'rings5: the four lowest modes tuned to consonant intervals'))
# rings6, Chromatic: every mode onto the nearest semitone of the note.
write_variant('rings6', 0.0, 'none: level with rings2 at every STRETCH', tune_tables([CHROMATIC] * 8, 'rings6: every mode tuned to the nearest semitone'))

