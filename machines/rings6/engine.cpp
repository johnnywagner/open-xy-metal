// rings6: rings2 tuned. STRETCH places the modes as rings2 does; every mode then moves to the nearest semitone of the
// note (12-TET, counted from the note): rings2's metal, every partial in tune. Turning STRETCH steps them.
// Everything else is rings2 (machines/rings2): the same engine, chain, half-rate modes, make-up and CPU.
// A fixed-point port of the float reference engine (rings.c in the port kit), with its constants (tables.h, generated
// by tools/gen_tables.py), simplified for the OP-XY's CPU.
//
// Per 64-sample block:
//   FM core at 1x, in four 16-sample steps (the drop, BITE and the onset move per step, as in the reference)
//     (op2 and its one-poles at half rate while op2 is below 260 Hz; sines read from a table without interpolation)
//   -> the chain's high-pass on the drive
//   -> MODAL: up to eight undamped two-pole oscillators, damped every 16 samples (+ STRIKE). Modes below fs/8 run at
//      half rate, fed once per four samples, and are interpolated back (8-tap half-band); the others are fed once
//      per two samples. The mix runs 8 samples late, for the interpolation.
//   -> mix: core + modes -> both fixed 20.7 kHz low-passes as one 3-tap FIR (shifts) -> the chain's band, which
//      carries every output gain -> half-scale ceiling
//
// The chain: the reference's two keytracked high-passes, presence peak and high shelf (sixth order) are fitted as two
// biquads in units of the note (tools/gen_tables.py, CHAIN; within 1.4 dB from 0.2 x the note): a resonant high-pass,
// which shapes the drive as the reference's first high-pass and presence peak do, and a band after the modes. The
// reference's STRIKE skips the filters before the modes; here it is rotated per mode by the gain it misses.
//
// The OP-XY gives a voice no note-off, so the stock amp envelope shapes the whole sound: the voice runs as the
// reference does at ATTACK 0 and SUSTAIN 1, struck at full strength. It keeps the reference's 3 ms eased onset.
//
// Fixed point throughout: Q31 signals, coefficients made per note and per knob change from table-based sine, log2
// and exp2 (fixmath.h). No float, no division and no 64-bit arithmetic in the audio path. Recursive filters and the
// low modes keep the small quantities near their poles as their coefficients (2 + a1, 1 - a2, 2 - 2 cos th), so low
// notes keep their precision. Signed right shifts are arithmetic (GCC and Clang define them so).
#include "core.h"
#include "fixmath.h"

namespace rings6 {
namespace {

const unsigned STEP = 16;               // the reference's sub-block
const unsigned STEPS = opxy::block_frames / STEP;

inline i32 knob_word(i32 w) { return w < 0 ? 0 : w; }
inline i32 mul27(i32 a, i32 c) { return mul(a, c) * 16; }
// sin from the 4096-point Q15 table, no interpolation, scaled to Q(15 + s).
template <int S> inline i32 sin16(u32 phase) { return static_cast<i32>(SINE16[phase >> 20]) * (1 << S); }

// ---------------------------------------------------------------- filters
// The half angle of a cutoff given as a phase increment (2^32 = the sample rate): h = sin(w0/2), k = cos(w0/2), Q31.
inline void half_angle(u32 inc, i32& h, i32& k) {
    h = sine(inc >> 1) * 2;
    k = cosine(inc >> 1) * 2;
}
// The chain, both keytracked high-passes, the presence peak and the shelf, as two biquads fitted in units of the note
// (tools/gen_tables.py, CHAIN): a high-pass s^2 / (s^2 + a1 s + a0) and a band s (b2 s + b1) / (s^2 + c1 s + c0), made
// digital per note by the bilinear transform warped at the note, u = tan(pi f0 / fs). With z = z^-1 and
// D = 1 + a1 u + a0 u^2, a pole pair is 2 + a1' = 2 (a1 u + 2 a0 u^2) / D and 1 - a2' = 2 a1 u / D (each a product, from
// logs: no cancellation at low notes, where u is tiny). The high-pass runs as (1 - z)^2 without its 1 / D; the band's
// numerator, (b2 (1 - z)^2 + b1 u (1 - z^2)) / D, carries that, the output gain and a 2 for the half sums it takes.
// ch_c: high-pass 2 + a1' (Q30), 1 - a2' (Q31); band b2 and b1 u terms (Q27), 2 + a1' (Q29), 1 - a2' (Q31).
void setup_chain(Voice& v, u32 inc) {
    i32 h, c; half_angle(inc, h, c);
    const i32 lu = log2q(h, 31) - log2q(c, 31), one = 1 << 27;
    const i32 p1 = exp2q(L_CH_A1 + lu, 27), p2 = exp2q(L_CH_A0 + 2 * lu, 27);
    const i32 ld1 = log2q(one + p1 + p2, 27);
    v.ch_c[0] = exp2q(ONE_LOG + lu + log2q(exp2q(L_CH_A1, 27) + exp2q(L_CH_A0 + lu + ONE_LOG, 27), 27) - ld1, 30);   // 2u (a1 + 2 a0 u) / D
    v.ch_c[1] = exp2q(ONE_LOG + L_CH_A1 + lu - ld1, 31);
    const i32 q1 = exp2q(L_CH_C1 + lu, 27), q2 = exp2q(L_CH_C0 + 2 * lu, 27);
    const i32 ld2 = log2q(one + q1 + q2, 27), lg = L_OUT - ld1 - ld2 + ONE_LOG;
    v.ch_c[2] = exp2q(L_CH_B2 + lg, 27);
    v.ch_c[3] = exp2q(L_CH_B1 + lu + lg, 27);
    v.ch_c[4] = exp2q(ONE_LOG + lu + log2q(exp2q(L_CH_C1, 27) + exp2q(L_CH_C0 + lu + ONE_LOG, 27), 27) - ld2, 29);
    v.ch_c[5] = exp2q(ONE_LOG + L_CH_C1 + lu - ld2, 31);
}

// ---------------------------------------------------------------- knobs
// A DROP lane: 0..1 along it, split at DROP_SPLIT, each side a power curve.
i32 lane(i32 x, i32 mid, i32 e1, i32 e2) {
    if (x <= DROP_SPLIT_Q31) {
        const i32 y = clamp(mul29(x, DROP_SPLIT_INV), 0, MAX);
        return y ? mul(mid, exp2q(mul29(log2q(y, 31), e1), 31)) : 0;
    }
    const i32 y = clamp(mul29(x - DROP_SPLIT_Q31, DROP_HI_INV), 0, MAX);
    return mid + mul(MAX - mid, y ? exp2q(mul29(log2q(y, 31), e2), 31) : 0);
}
void set_drop(Voice& v, i32 x) {
    i32 oct = SW_A0 + mul(SW_SPAN, lane(x, SW_MID, SW_E1, SW_E2));
    if (absolute(oct) < SW_DEAD) oct = 0;
    v.sweep_oct = oct;
    v.sweep_du = exp2q(L_DT_T0 - mul(L_T1_T0, lane(x, TM_MID, TM_E1, TM_E2)), 31);
}
// The nearest semitone to l (log2 of a ratio to the note, Q24) whose pitch class mask allows, as log2, Q24. Every
// gap in the masks is at most three semitones, so the two either side of the nearest cover it.
i32 tune(i32 l, u32 mask) {
    const i32 s = l * 12;                                  // Q24 semitones (|l| < 8 octaves: no overflow)
    const i32 n0 = sar(s + (1 << 23), 24);
    i32 best = n0, gap = MAX;
    for (i32 n = n0 - 2; n <= n0 + 2; ++n) {
        i32 pc = n;
        while (pc < 0) pc += 12;
        while (pc >= 12) pc -= 12;
        if (!((mask >> pc) & 1u)) continue;
        const i32 d = absolute(s - n * (1 << 24));
        if (d < gap) { gap = d; best = n; }
    }
    return best * SEMI;
}
// STRETCH as rings2 (the reference): each mode at its material ratio, log2 scaled by 2^(2 x - 1); then tuned (TUNE_MASK),
// then its fixed detune. Shared with the machine's tests.
void stretch_map(i32 x, i32* lrr) {
    const i32 centred = x - 0x40000000;
    const i32 stretch = absolute(centred) < STRETCH_DEAD ? 0 : sar(centred, 6);   // 2 (x - 0.5), Q24
    const i32 st = exp2q(stretch, 29);                                           // 2^stretch, Q29
    for (unsigned k = 0; k < MODES; ++k) {
        const i32 l = mul29(L_RM[k], st);
        lrr[k] = (TUNE_MASK[k] ? tune(l, TUNE_MASK[k]) : l) + L_SPREAD[k];
    }
}
void set_stretch(Voice& v, i32 x) { stretch_map(x, v.lrr); }
void set_modes(Voice& v, i32 x) {
    const i32 m7 = mul(x, 7 << 28);                 // 7 x, Q28: mode k is fed clamp(1 + 7x - k, 0, 1)
    v.wk[0] = MAX;
    for (unsigned k = 1; k < MODES; ++k) {
        const i32 w = clamp(m7 - static_cast<i32>(k - 1) * (1 << 28), 0, 1 << 28);
        v.wk[k] = w == (1 << 28) ? MAX : w * 8;
    }
}

// ---------------------------------------------------------------- note set-up
// Keytracked filters for a pitch.
void setup_pitch(Voice& v, u32 inc) {
    v.setup_inc = inc;
    setup_chain(v, inc);
}
// Where the modes sit: frequency, edge fade and oscillator coefficient, for the pitch and STRETCH. The modes run
// undamped per sample; their decay is applied every 16 samples (DAMP16).
// Half rate: a mode below fs/8 can run at half rate (engine.cpp, modes_block). It is chosen at note-on (or while the
// mode is silent), below 7/8 of fs/8, and kept while the note rings unless the mode moves above fs/8: switching moves
// the mode's state between the two grids (below), which is exact, but the interpolation around the switch is not.
void setup_modes(Voice& v) {
    const i32 linc = log2u(v.setup_inc);
    const u32 was_half = v.half;
    v.high = 0;
    v.quad = 0;
    v.half = 0;
    for (unsigned k = 0; k < MODES; ++k) {
        const i32 l = linc + v.lrr[k];
        v.lfk[k] = clamp(l, L_INC_20HZ, L_INC_NY);
        i32 edge = 0;
        u32 fk = 0;
        if (l < L_INC_NY) {
            fk = exp2u(l);                              // below fs/4 here: no overflow
            if (fk > static_cast<u32>(INC_20HZ)) {
                edge = fk < 2u * static_cast<u32>(INC_20HZ)
                    ? exp2q(log2u(fk - static_cast<u32>(INC_20HZ)) - L_INC_20HZ, 31) : MAX;
                if (l > L_INC_NY09)
                    edge = mul(edge, exp2q(log2u(static_cast<u32>(INC_LIMIT) - fk) - L_INC_NY01, 31));
            }
        }
        v.edge[k] = edge;
        if (!edge) { v.y1[k] = v.y2[k] = 0; v.mc[k] = 0; v.sn[k] = 0; continue; }
        const bool silent = !v.y1[k] && !v.y2[k], was = (was_half >> k) & 1;
        const bool now = fk < static_cast<u32>(INC_EIGHTH)
                         && (v.note_pending || silent ? fk < static_cast<u32>(INC_EIGHTH) / 8u * 7u : was);
        i32 h, c; half_angle(fk, h, c);
        const i32 sn = 2 * mul(h, c);                   // sin th
        v.sn[k] = sn;
        v.lch[k] = log2q(c, 31);                        // log2 cos(th / 2)
        if (fk < static_cast<u32>(INC_EIGHTH)) { v.quad |= 1u << k; v.lcos[k] = log2q(cosine(fk) * 2, 31); }
        if (fk < static_cast<u32>(INC_SIXTH)) {
            v.mc[k] = mul(h, h) * 4;                    // 2 - 2 cos th = 4 sin^2(th / 2), Q31
        } else {
            v.mc[k] = cosine(fk) * 2;                   // 2 cos th, Q30
            v.high |= 1u << k;
        }
        v.leps[k] = sar(softplus2(L_EPS[k] - 2 * log2q(sn, 31)), 1);
        const i32 c1 = cosine(fk), cs = c1 >= 0x40000000 ? MAX : c1 * 2;   // cos th, Q31
        const i32 s2 = mul(sn, sn);                     // sin^2 th
        v.mh[k] = s2 * 2;                               // 4 sin^2 th, Q30
        if (now) { v.rc[k] = MAX - s2 * 2; v.rs[k] = mul(sn, cs) * 2; v.half |= 1u << k; }   // cos 2th, sin 2th
        else { v.rc[k] = cs; v.rs[k] = sn; }
        if (now != was && !silent) {
            // The full-rate state is (y[63], y[62]), the half-rate one (y[62], y[60]), with y[n-2] = 2 cos th y[n-1] - y[n]
            // (undamped) one way and y[61] = (y[62] + y[60]) / (2 cos th) the other.
            const i32 y1 = v.y1[k], y2 = v.y2[k];
            if (now) {
                const i32 y61 = mul(y2, cs) * 2 - y1;
                v.y1[k] = y2; v.y2[k] = mul(y61, cs) * 2 - y2;
            } else {
                const i32 y61 = mul(half(y1) + half(y2), exp2q(-log2q(cs, 31), 30)) * 2;
                v.y1[k] = mul(y1, cs) * 2 - y61; v.y2[k] = y1;
            }
        }
        // STRIKE's correction, 1 / (HP PK) at the mode's ratio to the note: tables in 1/16 octaves from 2^-4
        const i32 pos = clamp(l - linc + 4 * ONE_LOG, 0, 10 * ONE_LOG - 1);
        const i32 at = pos >> 20, frac = (pos & 0xfffff) << 11;
        v.gre[k] = STRIKE_GRE[at] + mul(STRIKE_GRE[at + 1] - STRIKE_GRE[at], frac);
        v.gim[k] = STRIKE_GIM[at] + mul(STRIKE_GIM[at + 1] - STRIKE_GIM[at], frac);
    }
}
// |d| folded onto the FM lattice spacing sp: distance to the nearest lattice line (Q26).
i32 fold(i32 d, i32 sp) {
    if (d >= sp) {
        const u32 n = exp2u(log2u(static_cast<u32>(d)) - log2u(static_cast<u32>(sp)));
        d = static_cast<i32>(static_cast<u32>(d) - n * static_cast<u32>(sp));
        if (d < 0) d += sp;
        if (d >= sp) d -= sp;
    }
    return 2 * d > sp ? sp - d : d;
}
// log2(1 + X^2), X = 2 d f0 / bw: how far a mode at distance d (Q26, in notes) sits off the lattice, in bandwidths.
inline i32 off_lattice(i32 d, i32 lhz, i32 lbw) {
    return d ? softplus2(2 * (ONE_LOG + log2q(d, 26) + lhz - lbw)) : 0;
}
// Make-up gain per mode, as the reference: modes moved off the FM's harmonic lattice catch less of it (at most 18 dB
// either way), and modes moved up get up to 12 dB more, (ratio)^0.75.
void setup_makeup(Voice& v, i32 ratio_word) {
    const i32 linc = log2u(v.setup_inc);
    const i32 lhz = linc + L_FS - 32 * ONE_LOG;
    const i32 sp = (ratio_word > RATIO_MIN ? ratio_word : MAX) >> 5;   // Q26
    for (unsigned k = 0; k < MODES; ++k) {
        const i32 dm = fold(RM_DIST[k], sp);
        const u32 rel = exp2u(v.lfk[k] - linc + 26 * ONE_LOG);
        const i32 ratio_k = rel > 0x7fffffffu ? MAX : static_cast<i32>(rel);
        const i32 da = fold(absolute(ratio_k - (1 << 26)), sp);
        i32 lmk = clamp(sar(off_lattice(da, lhz, L_BW[k]) - off_lattice(dm, lhz, L_BW[k]), 1),
                        -3 * ONE_LOG, 3 * ONE_LOG);
        const i32 up = v.lfk[k] - linc - L_RM[k];
        if (up > 0) { const i32 t = mul(up, UP_POWER); lmk += t < 2 * ONE_LOG ? t : 2 * ONE_LOG; }
        v.mk_target[k] = exp2q(lmk, 25);
        if (!v.mk[k]) v.mk[k] = v.mk_target[k];
    }
    v.gx_dirty = 1;
}
// Each mode's input gain: the reference's b = (1 - r) |1 - r e^(2 j th)| ra RES_GAIN edge make-up MODES, in log2.
// The input arrives once per two samples as the pair's sum, whose gain at the mode is cos(th / 2) per sample; modes
// below fs/8 take it once per four samples through [1/2 1 1 1 1/2], whose gain is cos th cos^2(th / 2) per sample.
void update_b(Voice& v) {
    for (unsigned k = 0; k < MODES; ++k) {
        i32 l = L_B[k] + log2q(v.sn[k], 31) + v.leps[k] + log2q(v.mk[k], 25) - v.lch[k];
        if ((v.half >> k) & 1) l += ONE_LOG - v.lch[k];   // half rate: 2 cos th x the quad gain (modes_block)
        else if ((v.quad >> k) & 1) l -= v.lcos[k] + v.lch[k];
        v.b[k] = v.edge[k] && v.wk[k] ? mul(mul(exp2q(l, 31), v.edge[k]), v.wk[k]) : 0;
    }
    v.gx_dirty = 0;
}

// ---------------------------------------------------------------- the onset
// The reference's shortest attack: the level rises 3 ms in a straight line from where the note found it, eased in.
// Each stage applies its square root; returns the gain after this 16-sample step.
i32 onset_step(Voice& v) {
    if (!v.ramping) return MAX;
    v.att_u = v.att_u >= MAX - ATTACK_STEP ? MAX : v.att_u + ATTACK_STEP;
    const u32 j = v.att_j++;
    const i32 w = j < sizeof EASE / sizeof EASE[0] ? EASE[j] : MAX;
    if (v.att_u == MAX && w == MAX) { v.ramping = 0; return MAX; }
    return v.att_g0 + mul(sqrt31(v.att_u) - v.att_g0, w);
}

// ---------------------------------------------------------------- FM core
// One 16-sample step: op2 with self-feedback phase-modulates the carrier through two one-poles; the drop multiplies
// both increments. The reference runs this at 2x and decimates with a half-band; here it runs at 1x with the 2x
// rate's quantities (op2's sample difference halved, the carrier's phase offset doubled, the one-poles at their 1x
// coefficient). While op2 is below 260 Hz (low RATIO), op2 and its one-poles run at half rate. Writes the
// reference's x.
void core_step(Voice& v, i32* x, u32 base, i32 ratio) {
    u32 inc0 = base;
    if (v.sweep_oct) {
        if (!v.sweep_u) {
            inc0 = exp2u(log2u(inc0) + v.sweep_oct);   // nothing of the drop done yet
        } else if (v.sweep_u < MAX) {
            const i32 lu = log2q(v.sweep_u, 31);
            const i32 left = MAX - exp2q(lu + mul(lu, SWEEP_E), 31);   // 1 - u^e
            inc0 = exp2u(log2u(inc0) + mul(v.sweep_oct, left));
        }
        if (inc0 > static_cast<u32>(INC_DROP_LIMIT)) inc0 = static_cast<u32>(INC_DROP_LIMIT);
        if (v.sweep_u < MAX) {                         // u = j du; the drop never takes more than 256 steps
            const u32 u = static_cast<u32>(v.sweep_du >> 4) * ++v.sweep_j;   // Q27
            v.sweep_u = u >= (1u << 27) || v.sweep_j >= 256 ? MAX : static_cast<i32>(u << 4);
        }
    }
    const u32 inc2 = static_cast<u32>(mul(static_cast<i32>(inc0 >> 1), ratio)) * 2u;
    const i32 depth = DEPTH0 + mul(DEPTH_BITE, v.bite);   // Q29
    v.bite = mul(v.bite, BITE_DECAY);
    const i32 diff = static_cast<i32>(inc0 - v.inc_prev);
    const u32 dinc = static_cast<u32>(diff >= 0 ? diff >> 4 : -((-diff) >> 4));
    u32 inc = v.inc_prev, ph0 = v.ph0, ph2 = v.ph2;
    i32 s2p = v.s2p, s2pp = v.s2pp, l1 = v.l1, l2 = v.l2;
    if (inc2 < static_cast<u32>(OP2_HALF_RATE)) {
        // op2 and its one-poles at half rate: its difference quartered, its one-poles at their half-rate coefficient,
        // its feedback read where the 2x rate would read it
        for (unsigned k = 0; k < STEP; k += 2) {
            ph2 += 2u * inc2;
            const i32 fb = s2p + (s2p - s2pp) - ((s2p - s2pp) >> 2);
            const i32 s2 = sin16<15>(ph2 + static_cast<u32>(mul(fb, FB_PHASE)));
            const i32 d = (s2 - s2p) >> 2;
            s2pp = s2p; s2p = s2;
            const i32 m = l1;
            l1 = d - ((d - l1) >> 4);                  // l1 / 16 + 15 d / 16
            const u32 u = 2u * static_cast<u32>(l2);   // 0.25 u cycles per 2x sample: u in Q30
            const i32 dm = clamp(mul29(m, depth), -(1 << 30), (1 << 30) - 1);
            l2 = dm - ((dm - l2) >> 4);
            inc += dinc;
            x[k] = sin16<12>(ph0);                     // the carrier at 1/16 full scale: x = car / 2
            ph0 += inc + u;
            inc += dinc;
            x[k + 1] = sin16<12>(ph0);
            ph0 += inc + u;
        }
    } else {
        for (unsigned k = 0; k < STEP; ++k) {
            ph2 += inc2;
            const i32 s2 = sin16<15>(ph2 + static_cast<u32>(mul(s2p, FB_PHASE)));
            const i32 d = half(s2 - s2p);              // within Q30: both are
            s2pp = s2p; s2p = s2;
            const i32 m = l1;
            l1 = d - ((d - l1) >> 2);                  // 0.25 l1 + 0.75 d
            const i32 u = l2;
            const i32 dm = clamp(mul29(m, depth), -(1 << 30), (1 << 30) - 1);
            l2 = dm - ((dm - l2) >> 2);
            inc += dinc;
            x[k] = sin16<12>(ph0);
            ph0 += inc + 2u * static_cast<u32>(u);
        }
    }
    v.s2pp = s2pp;
    v.ph0 = ph0; v.ph2 = ph2; v.s2p = s2p; v.l1 = l1; v.l2 = l2; v.inc_prev = inc0;
}

// ---------------------------------------------------------------- MODAL
// Modes run undamped: y = 2 cos th y1 - y2, plus the input every other sample (and STRIKE); their decay, r^16, is
// applied every 16 samples. HIGH modes keep 2 cos th in Q30; low ones keep 2 - 2 cos th in Q31.
struct Mode { i32 y1, y2, c, b, damp; };
template <bool HIGH> inline i32 osc(i32 y1, i32 y2, i32 c) { return (HIGH ? mul30(y1, c) : 2 * y1 - mul(y1, c)) - y2; }

// Two modes together over 16 samples: they share the input and the accumulator.
template <bool HA, bool HB>
inline void pair_step(i32* acc, const i32* in, Mode& a, Mode& b, unsigned n0) {
    i32 a1 = a.y1, a2 = a.y2, b1 = b.y1, b2 = b.y2;
    for (unsigned n = n0; n < n0 + STEP; n += 2) {
        const i32 x = in[n >> 1];
        const i32 ya = osc<HA>(a1, a2, a.c) + mul(a.b, x);
        const i32 yb = osc<HB>(b1, b2, b.c) + mul(b.b, x);
        const i32 za = osc<HA>(ya, a1, a.c);
        const i32 zb = osc<HB>(yb, b1, b.c);
        acc[n] += ya + yb;
        acc[n + 1] += za + zb;
        a2 = ya; a1 = za; b2 = yb; b1 = zb;
    }
    a.y1 = mul(a1, a.damp); a.y2 = mul(a2, a.damp);
    b.y1 = mul(b1, b.damp); b.y2 = mul(b2, b.damp);
}
template <bool H>
inline void one_step(i32* acc, const i32* in, Mode& m, unsigned n0) {
    i32 y1 = m.y1, y2 = m.y2;
    for (unsigned n = n0; n < n0 + STEP; n += 2) {
        i32 y = osc<H>(y1, y2, m.c) + mul(m.b, in[n >> 1]);
        i32 z = osc<H>(y, y1, m.c);
        acc[n] += y; acc[n + 1] += z;
        y2 = y; y1 = z;
    }
    m.y1 = mul(y1, m.damp); m.y2 = mul(y2, m.damp);
}
// Modes below fs/8: the input once per four samples, at 4m + 1, centred on the five samples it sums.
inline void quad_pair_step(i32* acc, const i32* in4, Mode& a, Mode& b, unsigned n0) {
    i32 a1 = a.y1, a2 = a.y2, b1 = b.y1, b2 = b.y2;
    for (unsigned n = n0; n < n0 + STEP; n += 4) {
        const i32 x = in4[n >> 2];
        i32 ya = osc<false>(a1, a2, a.c), yb = osc<false>(b1, b2, b.c);
        acc[n] += ya + yb;
        i32 za = osc<false>(ya, a1, a.c) + mul(a.b, x), zb = osc<false>(yb, b1, b.c) + mul(b.b, x);
        acc[n + 1] += za + zb;
        a2 = osc<false>(za, ya, a.c); b2 = osc<false>(zb, yb, b.c);
        acc[n + 2] += a2 + b2;
        a1 = osc<false>(a2, za, a.c); b1 = osc<false>(b2, zb, b.c);
        acc[n + 3] += a1 + b1;
    }
    a.y1 = mul(a1, a.damp); a.y2 = mul(a2, a.damp);
    b.y1 = mul(b1, b.damp); b.y2 = mul(b2, b.damp);
}
inline void quad_one_step(i32* acc, const i32* in4, Mode& m, unsigned n0) {
    i32 y1 = m.y1, y2 = m.y2;
    for (unsigned n = n0; n < n0 + STEP; ++n) {
        i32 y = osc<false>(y1, y2, m.c);
        if ((n & 3) == 1) y += mul(m.b, in4[n >> 2]);
        acc[n] += y;
        y2 = y1; y1 = y;
    }
    m.y1 = mul(y1, m.damp); m.y2 = mul(y2, m.damp);
}
inline void run_one(i32* acc, const i32* in, Mode& m, bool high, unsigned n0) {
    if (high) one_step<true>(acc, in, m, n0); else one_step<false>(acc, in, m, n0);
}
// Half rate (modes below fs/8, chosen at note-on): the oscillator steps by 2 th, 2 - 2 cos 2th = 4 sin^2 th in Q30,
// and writes h, the half-rate modes' sum at the even samples, which process() interpolates (8 taps) to the full rate.
// It takes the input once per four samples, the same sums by four (centred on 4m + 1) at 4m + 2, x 2 cos th: at the
// even samples that is exactly the full-rate mode fed by four (a single impulse rings sin(th (t - 4m)) either way).
inline i32 osc_half(i32 y1, i32 y2, i32 c) { return 2 * y1 - mul30(y1, c) - y2; }
inline void half_pair_step(i32* h, const i32* in4, Mode& a, Mode& b, unsigned n0) {
    i32 a1 = a.y1, a2 = a.y2, b1 = b.y1, b2 = b.y2;
    const i32 ca = a.c, cb = b.c, ba = a.b, bb = b.b;
    i32* o = h + (n0 >> 1);
    for (unsigned q = 0; q < STEP / 4; ++q) {
        const i32 x = in4[(n0 >> 2) + q];
        const i32 ya = osc_half(a1, a2, ca), yb = osc_half(b1, b2, cb);
        const i32 za = osc_half(ya, a1, ca) + mul(ba, x), zb = osc_half(yb, b1, cb) + mul(bb, x);
        o[2 * q] += ya + yb; o[2 * q + 1] += za + zb;
        a2 = ya; a1 = za; b2 = yb; b1 = zb;
    }
    a.y1 = mul(a1, a.damp); a.y2 = mul(a2, a.damp);
    b.y1 = mul(b1, b.damp); b.y2 = mul(b2, b.damp);
}
inline void half_one_step(i32* h, const i32* in4, Mode& m, unsigned n0) {
    i32 y1 = m.y1, y2 = m.y2;
    i32* o = h + (n0 >> 1);
    for (unsigned q = 0; q < STEP / 4; ++q) {
        const i32 y = osc_half(y1, y2, m.c), z = osc_half(y, y1, m.c) + mul(m.b, in4[(n0 >> 2) + q]);
        o[2 * q] += y; o[2 * q + 1] += z;
        y2 = y; y1 = z;
    }
    m.y1 = mul(y1, m.damp); m.y2 = mul(y2, m.damp);
}
// STRIKE, in the note's first 16 samples: the pulse rings through a separate copy of the mode (at the full rate, c its
// full-rate coefficient), which joins the mode rotated by G = gre + j gim (engine.cpp, setup_modes; the chain after the
// modes, against what STRIKE went through in the reference). For a ring A cos(w n + phi) held as (s1, s0) = (y[n], y[n-d])
// (d = 1, or 2 at half rate, w d the step), Im = (s0 - cos(wd) s1) / sin(wd) and the rotated ring is
// s1' = gre s1 - gim Im, s0' = cos(wd) s1' + gre (s0 - cos(wd) s1) + gim sin(wd) s1. The ring's own 16 samples go out
// unrotated: into acc, or at half rate its even samples into h.
inline void strike_step(i32* acc, i32* h, bool half_rate, bool high, i32 c, i32 damp, i32 p, i32 gre, i32 gim,
                        i32 rs, i32 rc, i32& r1, i32& r0) {
    i32 y1 = 0, y2 = 0, y12 = 0;
    for (unsigned n = 0; n < STEP; ++n) {
        const i32 y = (high ? osc<true>(y1, y2, c) : osc<false>(y1, y2, c)) + mul(p, STRIKE[n]);
        if (!half_rate) acc[n] += y;
        else if (!(n & 1)) h[n >> 1] += y;
        if (n == STEP - 4) y12 = y;
        y2 = y1; y1 = y;
    }
    // the state: at the full rate (y[15], y[14]), at half rate (y[14], y[12])
    const i32 s1 = mul(half_rate ? y2 : y1, damp), s0 = mul(half_rate ? y12 : y2, damp);
    const i32 d = s0 - mul(rc, s1);
    i32 im = 0;
    if (d) {
        const u32 a = exp2u(log2u(static_cast<u32>(absolute(d))) - log2q(rs, 31));
        im = a > 0x3fffffffu ? 0x3fffffff : static_cast<i32>(a);
        if (d < 0) im = -im;
    }
    r1 = (mul(gre, s1) - mul(gim, im)) * 4;            // G in Q29
    r0 = mul(rc, r1) + (mul(gre, d) + mul(gim, mul(rs, s1))) * 4;
}
inline void run_pair(i32* acc, const i32* in, Mode& a, bool ha, Mode& b, bool hb, unsigned n0) {
    if (ha) { if (hb) pair_step<true, true>(acc, in, a, b, n0); else pair_step<true, false>(acc, in, a, b, n0); }
    else { if (hb) pair_step<false, true>(acc, in, a, b, n0); else pair_step<false, false>(acc, in, a, b, n0); }
}

// Adds each mode into acc (full rate) or h (half rate, the even samples; process() interpolates). in holds the drive's
// pair sums, in4 its sums by four. With STRIKE, the pulse goes into the modes during the first samples of the note,
// shared between the audible modes by sqrt(ra). Returns whether any mode ran at half rate.
bool modes_block(Voice& v, const i32* in, const i32* in4, i32* acc, i32* h, bool strike) {
    Mode m[MODES];
    bool high[MODES];
    unsigned kind[MODES];                              // 0 half rate, 1 fed by four, 2 fed by two
    unsigned live = 0;
    i32 gp[MODES] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (strike) {
        i32 term[MODES], wsum = 0;
        for (unsigned k = 0; k < MODES; ++k) {
            term[k] = v.edge[k] && v.wk[k] ? mul(mul(SQRT_RA[k], v.edge[k]), v.wk[k]) : 0;
            wsum += term[k] >> 3;                      // Q28
        }
        const i32 lw = wsum < 268 ? 0 : log2q(wsum, 28);
        for (unsigned k = 0; k < MODES; ++k)
            gp[k] = term[k] ? exp2q(L_BP[k] + log2q(v.sn[k], 31) + log2q(term[k], 31) - lw, 31) : 0;
    }
    unsigned index[MODES];
    for (unsigned pass = 0; pass < 3; ++pass)          // half rate, then fed by four, then the rest
        for (unsigned k = 0; k < MODES; ++k) {
            if (!v.edge[k]) continue;
            if (!v.wk[k] && !v.y1[k] && !v.y2[k]) continue;   // not fed and silent
            const unsigned t = (v.half >> k) & 1 ? 0 : ((v.quad >> k) & 1 ? 1 : 2);
            if (t != pass) continue;
            Mode& d = m[live];
            d.y1 = v.y1[k]; d.y2 = v.y2[k]; d.c = t ? v.mc[k] : v.mh[k]; d.b = v.b[k]; d.damp = DAMP16[k];
            high[live] = (v.high >> k) & 1;
            kind[live] = t;
            index[live++] = k;
        }
    for (unsigned j = 0; j < STEPS; ++j) {
        const unsigned n0 = j * STEP;
        i32 r1[MODES], r0[MODES];
        const bool first = strike && !j;               // the note's first 16 samples: each mode's strike apart
        if (first)
            for (unsigned i = 0; i < live; ++i) {
                const unsigned k = index[i];
                r1[i] = r0[i] = 0;
                if (gp[k]) strike_step(acc, h, !kind[i], high[i], v.mc[k], DAMP16[k], gp[k], v.gre[k], v.gim[k],
                                       v.rs[k], v.rc[k], r1[i], r0[i]);
            }
        unsigned i = 0;
        for (; i + 1 < live && kind[i + 1] == 0; i += 2) half_pair_step(h, in4, m[i], m[i + 1], n0);
        if (i < live && kind[i] == 0) half_one_step(h, in4, m[i++], n0);
        for (; i + 1 < live && kind[i + 1] == 1; i += 2) quad_pair_step(acc, in4, m[i], m[i + 1], n0);
        if (i < live && kind[i] == 1) quad_one_step(acc, in4, m[i++], n0);
        for (; i + 1 < live; i += 2) run_pair(acc, in, m[i], high[i], m[i + 1], high[i + 1], n0);
        if (i < live) run_one(acc, in, m[i], high[i], n0);
        if (first) for (i = 0; i < live; ++i) { m[i].y1 += r1[i]; m[i].y2 += r0[i]; }   // the strike joins, rotated
    }
    bool any = false;
    for (unsigned i = 0; i < live; ++i) {
        const unsigned k = index[i];
        i32 y1 = m[i].y1, y2 = m[i].y2;
        if (!v.wk[k] && absolute(y1) < FLUSH && absolute(y2) < FLUSH) y1 = y2 = 0;
        v.y1[k] = y1; v.y2[k] = y2;
        any = any || !kind[i];
    }
    return any;
}

void clear(Voice& voice) {
    u32* words = reinterpret_cast<u32*>(&voice);
    for (unsigned i = 0; i < sizeof(Voice) / sizeof(u32); ++i) words[i] = 0;
}

// Taps for the machine's own tests: each stage of the last block. Null in the device and preview builds.
struct Taps { i32 core[64], drive[64], mix[64], out[64]; };

inline void process(Voice& v, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch, Taps* taps) {
    // ---- knobs
    const i32 drop = knob_word(parameters[0]), ratio_word = knob_word(parameters[1]);
    const i32 stretch = knob_word(parameters[2]), modes = knob_word(parameters[3]);
    bool remode = false, remakeup = false;
    if (!v.knobs_seen || drop != v.knob[0]) set_drop(v, drop);
    if (!v.knobs_seen || stretch != v.knob[2]) { set_stretch(v, stretch); remode = true; }
    if (!v.knobs_seen || modes != v.knob[3]) { set_modes(v, modes); v.gx_dirty = 1; }
    if (!v.knobs_seen || ratio_word != v.knob[1]) { v.ratio_eff = ratio_word; remakeup = true; }
    v.knob[0] = drop; v.knob[1] = ratio_word; v.knob[2] = stretch; v.knob[3] = modes; v.knobs_seen = 1;
    // ---- pitch: set up again when it moves more than about 3.4 cents (glide, bend, transpose)
    const u32 inc = pitch[0];
    const u32 moved = inc > v.setup_inc ? inc - v.setup_inc : v.setup_inc - inc;
    if (!v.setup_inc || moved > (v.setup_inc >> 9)) { setup_pitch(v, inc); remode = true; }
    if (remode) { setup_modes(v); remakeup = true; v.gx_dirty = 1; }
    if (remakeup) setup_makeup(v, ratio_word);
    for (unsigned k = 0; k < MODES; ++k) {             // make-up eases ~20 ms
        const i32 d = v.mk_target[k] - v.mk[k];
        if (!d) continue;
        v.mk[k] = absolute(d) < 256 ? v.mk_target[k] : v.mk[k] + mul(d, KK_MAKEUP);
        v.gx_dirty = 1;
    }
    if (v.gx_dirty) update_b(v);
    // ---- note-on
    const bool strike = v.note_pending != 0;
    if (strike) {
        v.note_pending = 0;
        const u32 base = pitch[0];
        if (v.sweep_oct) v.inc_prev = exp2u(log2u(base) + v.sweep_oct);   // start where the drop starts
        else if (!v.inc_prev) v.inc_prev = base;
    }
    // ---- FM core and the onset, per 16 samples
    i32 x[opxy::block_frames], gains[opxy::block_frames];
    const bool shaped = v.ramping || v.gain != MAX;
    for (unsigned j = 0; j < STEPS; ++j) {
        core_step(v, x + j * STEP, pitch[j * STEP], v.ratio_eff);
        if (shaped) {
            const i32 e0 = v.gain, e1 = onset_step(v);
            const i32 d = half(e1) - half(e0);
            for (unsigned n = 0; n + 1 < STEP; ++n) gains[j * STEP + n] = e0 + 2 * mul(d, static_cast<i32>(n + 1) << 27);
            gains[j * STEP + STEP - 1] = e1;
            v.gain = e1;
        }
    }
    if (taps) for (unsigned n = 0; n < opxy::block_frames; ++n) taps->core[n] = x[n];
    if (shaped) for (unsigned n = 0; n < opxy::block_frames; ++n) x[n] = mul(x[n], gains[n]);
    if (taps) for (unsigned n = 0; n < opxy::block_frames; ++n) taps->drive[n] = x[n];
    // ---- the chain's high-pass shapes the drive (as the reference's first high-pass and presence peak do, which
    // STRIKE skips): (1 - z)^2 over its poles, near them, without its 1 / D (the band carries it)
    {
        i32 x1 = v.chain[0], x2 = v.chain[1], p1 = v.chain[2], p2 = v.chain[3];
        const i32 c0 = v.ch_c[0], c1 = v.ch_c[1];
        for (unsigned n = 0; n < opxy::block_frames; ++n) {
            const i32 in = x[n];
            const i32 p = in - 2 * x1 + x2 + 2 * p1 - p2 - mul30(p1, c0) + mul(p2, c1);
            x2 = x1; x1 = in; p2 = p1; p1 = p;
            x[n] = p;
        }
        v.chain[0] = x1; v.chain[1] = x2; v.chain[2] = p1; v.chain[3] = p2;
    }
    // ---- the modes' input: the core's pair sums, and its sums by four, [1/2 1 1 1 1/2] around 4m + 1
    const unsigned N = opxy::block_frames;
    i32 in[N / 2], in4[N / 4], acc[8 + N], hs[7 + N / 2], buf[N + 2];
    for (unsigned n = 0; n < 8; ++n) acc[n] = v.dly[n];
    for (unsigned n = 0; n < N; n += 2) {
        in[n >> 1] = x[n] + x[n + 1];
        acc[8 + n] = 2 * x[n]; acc[9 + n] = 2 * x[n + 1];   // the mix is half the accumulator: core + modes / 2
    }
    {
        i32 last = v.core_last;
        for (unsigned q = 0; q < N / 4; ++q) {
            const i32* d = x + 4 * q;
            in4[q] = (last >> 1) + d[0] + d[1] + d[2] + (d[3] >> 1);
            last = d[3];
        }
        v.core_last = last;
    }
    // ---- MODAL, mixed with the core. The mix runs 8 samples late: the half-rate modes' interpolation (the half-band
    // Lagrange interpolator, 8 taps: 1225, -245, 49, -5 / 2048 a side) reaches 4 of their samples ahead.
    bool hist = false;
    for (unsigned i = 0; i < 7; ++i) { hs[i] = v.hist[i]; hist = hist || hs[i]; }
    for (unsigned i = 7; i < 7 + N / 2; ++i) hs[i] = 0;
    const bool any = modes_block(v, in, in4, acc + 8, hs + 7, strike);
    for (unsigned n = 0; n < N; ++n) buf[2 + n] = acc[n];
    if (any || hist)
        for (unsigned m = 0; m < N / 2; ++m) {
            const i32* p = hs + m;                     // p[3], p[4]: the samples either side of 2m + 1, 8 late
            buf[2 + 2 * m] += p[3];
            buf[3 + 2 * m] += 2 * (1225 * ((p[3] >> 12) + (p[4] >> 12)) - 245 * ((p[2] >> 12) + (p[5] >> 12))
                                   + 49 * ((p[1] >> 12) + (p[6] >> 12)) - 5 * ((p[0] >> 12) + (p[7] >> 12)));
        }
    for (unsigned n = 0; n < 8; ++n) v.dly[n] = acc[N + n];
    for (unsigned i = 0; i < 7; ++i) v.hist[i] = hs[N / 2 + i];
    if (taps) for (unsigned n = 0; n < N; ++n) taps->mix[n] = half(buf[2 + n]);
    // ---- the output chain: low-pass FIR (a sample late; it also halves the accumulator), the onset's second gain, the
    // band, the ceiling
    {
        buf[0] = v.fir[0]; buf[1] = v.fir[1];
        v.fir[0] = buf[N]; v.fir[1] = buf[N + 1];
        i32 g1 = v.chain[4], g2 = v.chain[5], e1 = v.chain[6], e2 = v.chain[7];
        const i32 k0 = v.ch_c[2], k1 = v.ch_c[3], k2 = v.ch_c[4], k3 = v.ch_c[5];
        for (unsigned n = 0; n < N; ++n) {
            const i32* b = buf + n;
            const i32 dd = (b[0] >> 1) + (b[2] >> 1) - b[1];
            i32 g = (b[1] >> 1) + (dd >> 3) + (dd >> 9);   // h = LP_FIR = 1/8 + 1/512
            if (shaped) g = mul(g, gains[n]);
            const i32 hg = g >> 1, hg2 = g2 >> 1;
            const i32 e = mul27(hg - g1 + hg2, k0) + mul27(hg - hg2, k1)
                        + static_cast<i32>(2u * static_cast<u32>(e1) - static_cast<u32>(e2)) - mul29(e1, k2) + mul(e2, k3);
            g2 = g1; g1 = g; e2 = e1; e1 = e;
            output[n] = clamp(e, -HALF_SCALE, HALF_SCALE);
        }
        v.chain[4] = g1; v.chain[5] = g2; v.chain[6] = e1; v.chain[7] = e2;
    }
    if (taps) for (unsigned n = 0; n < opxy::block_frames; ++n) taps->out[n] = output[n];
}
} // namespace

inline void initialize(Voice& voice, Shared&, u32) { clear(voice); }

// Each note: the oscillators and filters keep running (as in the reference); the drop, BITE and STRIKE start again,
// and the onset rises from wherever the level is (from silence on a fresh voice).
inline void start(Voice& voice, Shared&, u32) {
    voice.note_pending = 1;
    voice.sweep_j = 0;
    voice.sweep_u = 0;
    voice.bite = MAX;
    const i32 level = voice.gain == MAX ? MAX : mul(voice.gain, voice.gain);
    voice.ramping = level != MAX;
    voice.att_u = level;
    voice.att_g0 = voice.gain;
    voice.att_j = 0;
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& voice, Shared&, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch pitch) {
    process(voice, output, parameters, pitch, static_cast<Taps*>(0));
}

} // namespace rings6
