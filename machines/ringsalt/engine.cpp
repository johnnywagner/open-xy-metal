// ringsalt: rings' voice (the FM exciter that drops onto each note, its chain and onset) with a tuned feedback
// delay for a resonator instead of eight modes: all the note's harmonics at once, for a fixed cost.
//
// Per 64-sample block:
//   FM core at 1x, in four 16-sample steps (as rings)
//   -> the chain's high-pass on the drive
//   -> the loop, at half rate: a delay line tuned to the note, read through a first-order allpass fractional delay,
//      two dispersion allpasses (STRETCH right: the partials stretch up, stiff-string style), a one-pole low-pass
//      (TONE) that also carries the loss (the ring time of rings' first mode), the drive and STRIKE added in. STRETCH
//      left mixes in a second tap at half the period, inverted: the even harmonics fade out (hollow, odd only).
//      Interpolated back to the full rate (8-tap half-band); the mix runs 8 samples late for it.
//   -> mix: core + loop -> the low-pass FIR -> the chain's band, which carries every output gain -> half-scale ceiling
//
// Fixed point throughout, as rings: Q31 signals, set-up from table sine, atan, log2 and exp2. No float, no division and
// no 64-bit arithmetic in the audio path.
#include "core.h"
#include "fixmath.h"

namespace ringsalt {
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
// RATIO (op2 : carrier, 0..1) pulled onto simple ratios: within MAGNET_W octaves of a magnet, the distance d becomes
// d^3 / W^2 (flat at the magnet, continuous at the edge); elsewhere it is free. Shared with the machine's tests.
i32 ratio_magnet(i32 x) {
    if (x <= 0) return 0;
    const i32 lx = log2q(x, 31);
    unsigned best = 0;
    for (unsigned i = 1; i < sizeof L_MAGNET / sizeof L_MAGNET[0]; ++i)
        if (absolute(lx - L_MAGNET[i]) < absolute(lx - L_MAGNET[best])) best = i;
    const i32 d = lx - L_MAGNET[best];
    if (absolute(d) >= MAGNET_W) return x;
    const i32 t = d * MAGNET_INV;                       // d / W, Q31
    return exp2q(L_MAGNET[best] + mul(d, mul(t, t)), 31);
}
// STRETCH, c = 2 x - 1: within STRETCH_HARM of the middle the loop is plain (the harmonic series); to the right the
// dispersion grows (amount), to the left the odd-harmonic tap (odd), each over the rest of its half.
void set_stretch(Voice& v, i32 x) {
    const i32 c = static_cast<i32>(static_cast<u32>(x - 0x40000000) * 2u);
    const i32 a = c == static_cast<i32>(0x80000000u) ? MAX : absolute(c);
    const i32 s = a < STRETCH_HARM ? 0 : clamp(mul29(a - STRETCH_HARM, ALT_SPAN_INV), 0, MAX);
    const i32 was = v.odd;
    v.amount = c > 0 ? s : 0;
    v.odd = c < 0 ? s : 0;
    if (!was && v.odd) v.f2x = v.f2y = 0;
}
// TONE: how much faster the harmonics die than the note, D = 8 (1 - x)^2: at full, none (every harmonic rings as long as
// the note); at zero the 4th harmonic dies 25 times as fast. setup_loop designs the low-pass for it, per note.
void set_tone(Voice& v, i32 x) { const i32 d = MAX - x; v.b = mul(d, d); }   // (1 - x)^2, Q31 (v.b: the knob's term)

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

// ---------------------------------------------------------------- the loop's tuning
// atan(num / den) as a phase (2^32 per cycle) for den > 0: the ATAN table (257 points over 0..1, interpolated), and
// pi/2 - atan(den / num) above 1.
i32 atan_phase(i32 num, i32 den) {
    if (!num) return 0;
    const i32 l = log2u(static_cast<u32>(absolute(num))) - log2u(static_cast<u32>(den));
    const u32 t = exp2u((l <= 0 ? l : -l) + 31 * ONE_LOG);         // the ratio or its inverse, Q31, at most 1
    const u32 i = t >> 23, f = (t & 0x7fffffu) << 8;
    const i32 r0 = i >= 256 ? ATAN[256] : ATAN[i] + mul(ATAN[i + 1] - ATAN[i], static_cast<i32>(f));
    const i32 r = l <= 0 ? r0 : (1 << 30) - r0;
    return num < 0 ? -r : r;
}
// A first-order allpass's phase delay at the note, (a + z^-1) / (1 + a z^-1): 1 - 2 atan2(a sin w, 1 + a cos w) / w,
// Q16 samples. ih is w as a phase increment, sw and cw its sine and cosine (Q30).
i32 allpass_delay(i32 a, u32 ih, i32 sw, i32 cw) {
    const i32 ph = atan_phase(mul(a, sw), (1 << 30) + mul(a, cw));
    if (!ph) return 1 << 16;
    const i32 d = static_cast<i32>(exp2u(log2u(static_cast<u32>(absolute(ph))) - log2u(ih) + 17 * ONE_LOG));
    return ph > 0 ? (1 << 16) - d : (1 << 16) + d;
}
// The whole and fractional parts of a delay D (Q16): n = floor(D - 1/2), d = D - n in [1/2, 3/2), as the first-order
// allpass (1 - d) / (1 + d), corrected once so its delay at the note is d.
void fractional(i32 D, u32 ih, i32 sw, i32 cw, i32& n, i32& a) {
    n = clamp((D - (1 << 15)) >> 16, 1, static_cast<i32>(ALT_LINE) - 4);
    const i32 d = D - (n << 16);
    i32 dd = d;
    for (int pass = 0; pass < 2; ++pass) {
        const i32 num = (1 << 16) - dd, den = (1 << 16) + dd;
        a = num ? exp2q(log2u(static_cast<u32>(absolute(num))) - log2u(static_cast<u32>(den)), 31) : 0;
        if (num < 0) a = -a;
        dd += d - allpass_delay(a, ih, sw, cw);
    }
}
// Tunes the loop to the note: the period less the dispersion's and the low-pass's delays at the note is the line plus
// the fractional allpass; the loss gives rings' first mode's ring time at the note (the low-pass's own loss there taken
// out); the drive is scaled by 1 - (the loop's gain at the note), so the note's resonance peaks at ALT_IN.
void setup_loop(Voice& v) {
    u32 ih = v.setup_inc < 0x3fffffffu ? v.setup_inc * 2u : 0x7ffffffeu;   // the note as a half-rate increment
    i32 P = static_cast<i32>(exp2u(48 * ONE_LOG - log2u(ih)));             // the period, Q16 half-rate samples
    while (P > (static_cast<i32>(ALT_LINE) - 16) << 16) { ih *= 2u; P >>= 1; }   // below the line's reach: an octave up
    const i32 sw = sine(ih), cw = cosine(ih);
    // dispersion: by note (half-octave rows from 16 Hz) and STRETCH amount (eighths), bilinear
    i32 alpha = 0;
    if (v.amount) {
        const i32 pos = clamp(2 * (log2u(ih >> 1) - ALT_L_F0), 0, 16 * ONE_LOG - 1);
        const i32 r = pos >> 24, rf = (pos & 0xffffff) << 7;
        const i32 a8 = mul(v.amount, 8 << 27);                              // Q27, 0..8
        const i32 c = a8 >= 8 << 27 ? 7 : a8 >> 27, cf = a8 >= 8 << 27 ? MAX : (a8 & 0x7ffffff) << 4;
        const i32* t = ALT_ALPHA + r * 9 + c;
        const i32 lo = t[0] + mul(t[1] - t[0], cf), hi = t[9] + mul(t[10] - t[9], cf);
        alpha = lo + mul(hi - lo, rf);
    }
    if (!v.alpha && alpha) v.dx[0] = v.dx[1] = v.dy[0] = v.dy[1] = 0;
    // the low-pass: the 4th harmonic loses e4 = 3 D (the note's loss per pass) more per pass, so its power falls by
    // K + 1 = 2^(2 e4) more; a one-pole b / (1 - c z^-1) does that where 4 c s^2 / (1 - c)^2 = K, s = sin(2 w) (the 4th
    // harmonic's half angle; 1 past the loop's Nyquist): c = K / (K + 2 s^2 + 2 s sqrt(K + s^2)).
    const i32 lpass = log2u(static_cast<u32>(P)) + log2u(static_cast<u32>(-ALT_L_R)) - 15 * ONE_LOG;   // log2 of the loss per pass, Q24 log of a Q24 value
    i32 c = 0;
    if (v.b) {
        const i32 l2e4 = ONE_LOG + log2q(v.b, 31) + L_24 + lpass;                // log2 (2 e4), e4 = 24 (1-x)^2 x the loss
        const i32 e2 = static_cast<i32>(exp2u(l2e4));                             // 2 e4, Q24
        const i32 K = e2 >= 10 * ONE_LOG ? (1 << 30) - (1 << 20) : static_cast<i32>(exp2u(e2 + 20 * ONE_LOG)) - (1 << 20);   // Q20
        const u32 q2 = ih < 0x40000000u ? ih * 2u : 0x40000000u;                // 2 w, at most pi / 2
        const i32 sq = sine(q2) >> 10;                                           // s, Q20
        if (K > 0 && sq > 0) {
            const i32 s2 = static_cast<i32>(exp2u(2 * log2u(static_cast<u32>(sq)) - 20 * ONE_LOG));   // s^2, Q20
            const i32 root = static_cast<i32>(exp2u(sar(log2u(static_cast<u32>(K + s2)) + 20 * ONE_LOG, 1)));   // sqrt, Q20
            const i32 sr = static_cast<i32>(exp2u(log2u(static_cast<u32>(sq)) + log2u(static_cast<u32>(root)) - 19 * ONE_LOG));   // 2 s root, Q20
            c = exp2q(log2u(static_cast<u32>(K)) - log2u(static_cast<u32>(K + 2 * s2 + sr)), 31);
        }
        if (c > ALT_C_MAX) c = ALT_C_MAX;
    }
    const i32 b = MAX - c;
    // the low-pass's delay at the note: atan2(c sin w, 1 - c cos w) / w
    const i32 ph = atan_phase(mul(c, sw), (1 << 30) - mul(c, cw));
    const i32 tlp = ph > 0 ? static_cast<i32>(exp2u(log2u(static_cast<u32>(ph)) - log2u(ih) + 16 * ONE_LOG)) : 0;
    i32 tdisp = alpha ? 2 * allpass_delay(alpha, ih, sw, cw) : 0;
    for (int tries = 0; alpha && tries < 8 && P - tdisp - tlp < ALT_P_MIN_Q16; ++tries) {   // keep some plain delay
        alpha = sar(alpha, 1);
        tdisp = 2 * allpass_delay(alpha, ih, sw, cw);
    }
    if (P - tdisp - tlp < ALT_P_MIN_Q16) { alpha = 0; tdisp = 0; }
    v.alpha = alpha;
    fractional(P - tdisp - tlp, ih, sw, cw, v.n1, v.a1);
    if (v.odd) fractional((P >> 1) - tlp, ih, sw, cw, v.n2, v.a2);
    // the loss: rings' first mode's pole per full-rate sample, over the period (two full-rate samples each)
    const i32 lg = -static_cast<i32>(exp2u(lpass));
    // the low-pass's gain at the note: b / sqrt(b^2 + 4 c sin^2(w / 2))
    i32 h, k; half_angle(ih, h, k);
    const i32 la = 2 * log2q(b, 31);
    const i32 lmag = c > 0 ? log2q(b, 31) - sar(la + softplus2(2 * ONE_LOG + log2q(c, 31) + 2 * log2q(h, 31) - la), 1) : 0;
    const i32 g = clamp(exp2q(lg - lmag, 31), 0, MAX - (1 << 12));
    v.kb = mul(g, b);
    v.kc = c;
    v.ig = mul(MAX - exp2q(lg, 31), ALT_IN) * 16;      // ALT_IN is Q27
    v.sg = ALT_STRIKE;
}

// ---------------------------------------------------------------- the loop
// One block at half rate: uin holds each step's input (the drive x its gain, and STRIKE); h gets the loop's output.
template <bool ODD, bool DISP>
void loop_run(Voice& v, const i32* uin, i32* h) {
    const u32 mask = ALT_LINE - 1;
    i32* line = v.line;
    u32 w = v.w;
    const u32 n1 = static_cast<u32>(v.n1), n2 = static_cast<u32>(v.n2);
    const i32 a1 = v.a1, a2 = v.a2, odd = v.odd, alpha = v.alpha, kb = v.kb, kc = v.kc;
    i32 f1x = v.f1x, f1y = v.f1y, f2x = v.f2x, f2y = v.f2y, dx0 = v.dx[0], dy0 = v.dy[0], dx1 = v.dx[1], dy1 = v.dy[1];
    i32 lp = v.lp;
    for (unsigned m = 0; m < opxy::block_frames / 2; ++m) {
        const i32 r1 = line[(w - n1) & mask];
        const i32 y1 = mul(a1, r1 - f1y) + f1x;
        f1x = r1; f1y = y1;
        i32 z = y1;
        if (ODD) {
            const i32 r2 = line[(w - n2) & mask];
            const i32 y2 = mul(a2, r2 - f2y) + f2x;
            f2x = r2; f2y = y2;
            z = y1 - mul(odd, y1 + y2);
        }
        if (DISP) {
            const i32 t0 = mul(alpha, z - dy0) + dx0; dx0 = z; dy0 = t0;
            const i32 t1 = mul(alpha, t0 - dy1) + dx1; dx1 = t0; dy1 = t1;
            z = t1;
        }
        lp = mul(z, kb) + mul(lp, kc);
        const i32 out = lp + uin[m];
        line[w & mask] = out;
        ++w;
        h[m] = out;
    }
    v.w = w; v.f1x = f1x; v.f1y = f1y; v.f2x = f2x; v.f2y = f2y;
    v.dx[0] = dx0; v.dy[0] = dy0; v.dx[1] = dx1; v.dy[1] = dy1; v.lp = lp;
}
void loop_block(Voice& v, const i32* uin, i32* h) {
    if (v.odd) { if (v.alpha) loop_run<true, true>(v, uin, h); else loop_run<true, false>(v, uin, h); }
    else { if (v.alpha) loop_run<false, true>(v, uin, h); else loop_run<false, false>(v, uin, h); }
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
    const i32 stretch = knob_word(parameters[2]), tone = knob_word(parameters[3]);
    bool retune = false;
    if (!v.knobs_seen || drop != v.knob[0]) set_drop(v, drop);
    if (!v.knobs_seen || stretch != v.knob[2]) { set_stretch(v, stretch); retune = true; }
    if (!v.knobs_seen || tone != v.knob[3]) { set_tone(v, tone); retune = true; }
    if (!v.knobs_seen || ratio_word != v.knob[1]) v.ratio_eff = ratio_magnet(ratio_word);
    v.knob[0] = drop; v.knob[1] = ratio_word; v.knob[2] = stretch; v.knob[3] = tone; v.knobs_seen = 1;
    // ---- pitch: set up again when it moves more than about 3.4 cents (glide, bend, transpose)
    const u32 inc = pitch[0];
    const u32 moved = inc > v.setup_inc ? inc - v.setup_inc : v.setup_inc - inc;
    if (!v.setup_inc || moved > (v.setup_inc >> 9)) { setup_chain(v, inc); v.setup_inc = inc; retune = true; }
    if (retune) setup_loop(v);
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
    // ---- the loop's input: the drive's pair sums (and STRIKE, in the note's first 16 samples)
    const unsigned N = opxy::block_frames;
    i32 uin[N / 2], acc[8 + N], hs[7 + N / 2], buf[N + 2];
    for (unsigned n = 0; n < 8; ++n) acc[n] = v.dly[n];
    for (unsigned n = 0; n < N; n += 2) {
        uin[n >> 1] = mul(x[n] + x[n + 1], v.ig);
        acc[8 + n] = 2 * x[n]; acc[9 + n] = 2 * x[n + 1];   // the mix is half the accumulator: core + loop / 2
    }
    if (strike)
        for (unsigned m = 0; m < 8; ++m) uin[m] += mul((STRIKE[2 * m] >> 1) + (STRIKE[2 * m + 1] >> 1), v.sg) * 16;   // sg is Q27
    // ---- the loop, at half rate; the mix runs 8 samples late, for its interpolation (the half-band Lagrange
    // interpolator, 8 taps: 1225, -245, 49, -5 / 2048 a side, reaches 4 of its samples ahead)
    for (unsigned i = 0; i < 7; ++i) hs[i] = v.hist[i];
    loop_block(v, uin, hs + 7);
    for (unsigned n = 0; n < N; ++n) buf[2 + n] = acc[n];
    for (unsigned m = 0; m < N / 2; ++m) {
        const i32* p = hs + m;                         // p[3], p[4]: the samples either side of 2m + 1, 8 late
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

} // namespace ringsalt
