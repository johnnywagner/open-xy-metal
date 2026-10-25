// rings6's own sound checks, compiled for the host and run by `python ../open-xy/sdk.py test --machine machines/rings6`.
//
// The reference engine (port-kit/engine/rings.c) is compiled into this test twice: as it is (float, the spec), and in
// double precision, where the machine's own algorithm is built on the reference's state and set-up (dbl::block_lite:
// the 1x FM core with op2 at half rate when slow, the merged low-pass FIR, the modes run undamped and fed in pairs). Every render in
// port-kit/reference/manifest.json goes through the Q31 port and that model, as the machine runs it (ATTACK 0,
// SUSTAIN 1: on the device the stock amp envelope shapes the sound), and they are compared sample by sample at four
// stages: the FM core, the resonator's drive, the mix after the modes, and the output. That checks the fixed point.
//
// Whether the simplified algorithm is still the reference's sound is compare.py's question: tools/check_renders.py
// renders the port and the float reference (this test, with an output folder) and compares them. The sample-level
// SNR against the float reference is printed alongside, for scale.
//
//   sound            the checks: per-stage SNR, peak bound, a held bend with retriggers; exit 1 on failure
//   sound OUTDIR     also write OUTDIR/<name>.wav (the port, 32-bit PCM) and OUTDIR/<name>.ref.wav (the float
//                    reference as the machine runs it, float WAV, at the machine's gain), for tools/check_renders.py
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <complex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "../../../port-kit/engine/rings.c"
namespace dbl {
#undef RINGS_H
#define float double
#define expf exp
#define powf pow
#define sqrtf sqrt
#define exp2f exp2
#define log2f log2
#define fabsf fabs
#define floorf floor
#define cosf cos
#define sinf sin
#define tanf tan
#define rings_init dbl_rings_init
#define rings_set_knob dbl_rings_set_knob
#define rings_set_gain dbl_rings_set_gain
#define rings_note_on dbl_rings_note_on
#define rings_note_off dbl_rings_note_off
#define rings_process dbl_rings_process
#define rings_active dbl_rings_active
#include "../../../port-kit/engine/rings.c"
// The machine's FM core runs at 1x with the 2x rate's quantities, op2 at half rate below 260 Hz (engine.cpp,
// core_step): the same, in double precision, on the reference's own state and helpers.
static float s2pp_state;                             // op2 two steps back (reset per render)
// The engine's FM sine: the 4096-point Q15 table, read without interpolation.
static float sin16d(uint32_t ph) { return rings6::SINE16[ph >> 20] / 32768.0; }
static void core_block_1x(rings_t* v, float* x)
{
    double incd = note_hz(v->note) / v->fs;
    if (v->sweep != 0.f) {
        const float s = sweep_left(v, v->sweep_t);
        incd *= exp2f(v->sweep * s / 12.f);
        if (incd > 0.4) incd = 0.4;
        v->sweep_t += (float)RINGS_BLOCK / v->fs;
    }
    const uint32_t inc0 = (uint32_t)(incd * 4294967296.0);
    const float f2 = v->frq2 >= 127.f ? 1.f : v->frq2 / 128.f;
    const uint32_t inc2 = (uint32_t)(incd * 4.0 * f2 * f2 * 4294967296.0);
    const float fb = 0.5f * FB2 / 128.f;
    const float e = ENV2 / 128.f;
    float depth = 4.f * e * e; if (depth > 1.f) depth = 1.f;
    depth *= 1.f + 3.f * BITE * expf(-v->bite_t / 0.025f);
    v->bite_t += (float)RINGS_BLOCK / v->fs;
    uint32_t inc = v->inc_prev; const int32_t dinc = (int32_t)(inc0 - v->inc_prev) / RINGS_BLOCK;
    uint32_t ph0 = v->ph0, ph2 = v->ph2;
    float s2p = v->s2_prev, l1 = v->lp1, l2 = v->lp2, s2pp = s2pp_state;
    if (inc2 < (uint32_t)rings6::OP2_HALF_RATE) {
        for (int k = 0; k < RINGS_BLOCK; k += 2) {    // op2 and its one-poles at half rate
            ph2 += 2u * inc2;
            const float fbx = s2p + 0.75f * (s2p - s2pp);
            const float s2 = sin16d(ph2 + cyc2u(fb * fbx));
            const float d = clip1(0.25f * (s2 - s2p)); s2pp = s2p; s2p = s2;
            const float m = l1; l1 = l1 / 16.f + 15.f / 16.f * d;
            const float u = l2; l2 = l2 / 16.f + 15.f / 16.f * clip1(depth * m);
            for (int i = 0; i < 2; ++i) {
                inc += (uint32_t)dinc;
                x[k + i] = 0.0625f * sin16d(ph0);
                ph0 += inc + 2u * cyc2u(0.25f * u);
            }
        }
    } else {
        for (int k = 0; k < RINGS_BLOCK; ++k) {
            ph2 += inc2;
            const float s2 = sin16d(ph2 + cyc2u(fb * s2p));
            const float d = clip1(0.5f * (s2 - s2p)); s2pp = s2p; s2p = s2;
            const float m = l1; l1 = 0.25f * l1 + 0.75f * d;
            const float u = l2; l2 = 0.25f * l2 + 0.75f * clip1(depth * m);
            inc += (uint32_t)dinc;
            x[k] = 0.0625f * sin16d(ph0);
            ph0 += inc + 2u * cyc2u(0.25f * u);
        }
    }
    s2pp_state = s2pp;
    v->ph0 = ph0; v->ph2 = ph2; v->s2_prev = s2p; v->lp1 = l1; v->lp2 = l2; v->inc_prev = inc0;
}
// The machine's MODAL (engine.cpp): the reference's modes, set up as it sets them up, run undamped with their decay
// applied every 16 samples, fed the pair sum x[n] + x[n+1] every other sample (b / cos(th/2)), and struck with the
// pulse through the fixed low-pass (the engine's STRIKE table, back in the reference's units). Modes below fs/8 take
// their input once per four samples.
static float last_x;                                 // the core's last sample of the previous 16
static double late[8], hh[7];                        // the mix's last 8 samples, the half-rate modes' last 7
// The chain after the modes, fitted (tools/gen_tables.py, CHAIN): a high-pass s^2 / (s^2 + a1 s + a0) and a band
// s (b2 s + b1) / (s^2 + c1 s + c0), s in units of the note.
static const float CH_A1 = 0.3066f, CH_A0 = 0.21686f, CH_B2 = 2.43331f, CH_B1 = 16.70592f, CH_C1 = 5.56414f, CH_C0 = 20.15355f;
// The bilinear transform warped at the note (u = tan(pi f0 / fs)) of (b2 s^2 + b1 s + b0) / (s^2 + a1 s + a0).
static void bilinear(rings_biquad* q, float b2, float b1, float b0, float a1, float a0, float u)
{
    const float D = 1.f + a1 * u + a0 * u * u;
    q->b0 = (b2 + b1 * u + b0 * u * u) / D; q->b1 = 2.f * (b0 * u * u - b2) / D; q->b2 = (b2 - b1 * u + b0 * u * u) / D;
    q->a1 = 2.f * (a0 * u * u - 1.f) / D; q->a2 = (1.f - a1 * u + a0 * u * u) / D;
}
// STRIKE's rotation, G = HP EQ / (PK BAND) at r times the note (analog prototypes; tools/gen_tables.py, strike_gain).
static void strike_g(float r, float* gr, float* gi)
{
    typedef std::complex<double> C;
    const C s(0.0, r), W = s / (double)HP_RATIO, hp = W * W / (W * W + W / (double)HP_Q + 1.0);
    const double A = pow(10.0, 12.0 * PRESENCE / 40.0);
    const C x = s / 4.0, pk = (x * x + x * A / 0.8 + 1.0) / (x * x + x / (0.8 * A) + 1.0);
    const C eq = (2.5799 * s * s + 11.3473 * s + 16.21) / (s * s + 3.4644 * s + 16.1646);
    const C band = ((double)CH_B2 * s * s + (double)CH_B1 * s) / (s * s + (double)CH_C1 * s + (double)CH_C0);
    const C g = hp * eq / (pk * band);
    *gr = g.real(); *gi = g.imag();
}
// STRETCH's tuned map: the engine's own (rings6::stretch_map), on the setting's knob word.
static opxy::i32 stretch_word;
static void stretch_model(rings_t* v)
{
    opxy::i32 lrr[8];
    rings6::stretch_map(stretch_word, lrr);
    for (int k = 0; k < 8; ++k) v->rr[k] = (float)std::exp2(lrr[k] / 16777216.0);
}
static void modal_block_lite(rings_t* v, float* x)
{
    if (v->stretch != v->rkey_stretch || v->note != v->rkey_note) { modal_setup(v); stretch_model(v); }
    float wk[8];
    for (int k = 0; k < 8; ++k) { const float w = v->modes - (float)k; wk[k] = k == 0 ? 1.f : (w > 1.f ? 1.f : (w < 0.f ? 0.f : w)); }
    const float T = 0.1f * powf(100.f, RDECAY);
    v->rT = 1.f;
    const float mix = WET, dry = 1.f - mix;
    const int pulse = (int)(sizeof rings6::STRIKE / sizeof rings6::STRIKE[0]);
    const int at = (int)(v->rst * v->fs + 0.5f);
    float pl[RINGS_BLOCK]; int strike = 0;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        pl[n] = at + n < pulse ? rings6::STRIKE[at + n] / 2147483648.0 * (DIST_PAD * G_FM) : 0.f;
        strike |= pl[n] != 0.f;
    }
    const float Wp = 0.001f * powf(0.2f, RTONE);
    const float f0n = (float)note_hz(v->note), ny = 0.25f * v->fs;   // the modes stop at fs/4
    v->rst += (float)RINGS_BLOCK / v->fs;
    float fk[8], ek[8], wsum = 0.f;
    for (int k = 0; k < 8; ++k) {
        float f = f0n * v->rr[k];
        float edge = 1.f;
        if (f < 40.f) { edge = (f - 20.f) / 20.f; if (edge < 0.f) edge = 0.f; f = f < 20.f ? 20.f : f; }
        if (f > ny * 0.9f) { const float e2 = (ny - f) / (0.1f * ny); edge *= e2 < 0.f ? 0.f : e2; if (f > ny) f = ny; }
        fk[k] = f; ek[k] = edge; wsum += sqrtf(v->ra[k]) * edge * wk[k];
    }
    if (wsum < 1e-6f) wsum = 1.f;
    float mkg[8];
    {   // make-up, as the reference: off the FM's lattice, and modes moved up (engine.cpp, setup_makeup)
        const float f2 = v->frq2 >= 127.f ? 1.f : v->frq2 / 128.f, rfm = 4.f * f2 * f2;
        const float fq = f0n;
        const float sp = fq * (rfm > 0.05f ? rfm : 1.f), kk = 1.f - expf(-(float)RINGS_BLOCK / (v->fs * 0.02f));
        for (int k = 0; k < 8; ++k) {
            const float bw = 6.9078f / (3.14159265f * T * v->rt[k] * v->rT) + 1.f;
            float dm = fabsf(f0n * v->rm[k] - f0n); dm -= sp * floorf(dm / sp); if (dm > 0.5f * sp) dm = sp - dm;
            float da = fabsf(fk[k] - fq); da -= sp * floorf(da / sp); if (da > 0.5f * sp) da = sp - da;
            const float cm = 1.f / sqrtf(1.f + 4.f * dm * dm / (bw * bw)), ca = 1.f / sqrtf(1.f + 4.f * da * da / (bw * bw));
            float mk = cm / ca; if (mk > 8.f) mk = 8.f; if (mk < 0.125f) mk = 0.125f;
            const float up = fk[k] / (fq * v->rm[k]);
            if (up > 1.f) { float t = powf(up, 0.75f); mk *= t > 4.f ? 4.f : t; }
            if (v->rmk[k] <= 0.f) v->rmk[k] = mk;
            v->rmk[k] += kk * (mk - v->rmk[k]); mkg[k] = v->rmk[k];
        }
    }
    float y[RINGS_BLOCK] = {0};
    double hb[RINGS_BLOCK / 2] = {0};                  // the half-rate modes, at the even samples
    for (int k = 0; k < 8; ++k) {
        const float f = fk[k], edge = ek[k];
        if (edge <= 0.f) { v->ry1[k] = v->ry2[k] = 0.f; continue; }
        if (wk[k] <= 0.f && v->ry1[k] == 0.f && v->ry2[k] == 0.f) continue;
        const float Tk = T * v->rt[k];
        const float r = expf(-6.9078f / (Tk * v->fs)), th = 6.2831853f * f / v->fs;
        const float c1 = 2.f * cosf(th);
        const float M = sqrtf(1.f - 2.f * r * cosf(2.f * th) + r * r);
        const float b = (1.f - r) * M * v->ra[k] * RES_GAIN * edge * mkg[k] * wk[k] / cosf(0.5f * th)
                        * (float)(rings6::RES_TRIM / 536870912.0);    // the modes' drive trim (Q29)
        const float bi = strike ? sinf(th) * sqrtf(v->ra[k]) * RES_GAIN * edge * wk[k] * 2.f / (Wp * v->fs * wsum) : 0.f;
        // The filters STRIKE used to skip now follow the modes: its ring runs apart for the first 16 samples, then is
        // rotated by G = 1 / (HP PK) at the mode and joins the mode (engine.cpp, strike_step)
        // Half rate below 7/8 of fs/8 (chosen at note-on in the engine; these renders hold one pitch): the state steps
        // by 2 th and holds the even samples; the sums by four (centred on 4m + 1) go in at 4m + 2, x 2 cos th
        const bool hr = th < 0.25 * 3.14159265358979323846 * 7.0 / 8.0;
        float s1 = 0.f, s2 = 0.f, s12 = 0.f;
        if (strike) {
            for (int n = 0; n < RINGS_BLOCK; ++n) {
                const float a = c1 * s1 - s2 + bi * pl[n];
                if (!hr) y[n] += a; else if (!(n & 1)) hb[n >> 1] += a;
                if (n == RINGS_BLOCK - 4) s12 = a;
                s2 = s1; s1 = a;
            }
        }
        float y1 = v->ry1[k], y2 = v->ry2[k];
        if (hr) {
            const float bq = b * cosf(0.5f * th) / (cosf(th) * cosf(0.5f * th) * cosf(0.5f * th)), u = 2.f * cosf(th) * bq;
            const float c2 = 2.f * cosf(2.f * th);
            for (int n = 0; n < RINGS_BLOCK; n += 2) {
                float a = c2 * y1 - y2;
                if ((n & 3) == 2)
                    a += u * (0.5f * (n == 2 ? last_x : x[n - 3]) + x[n - 2] + x[n - 1] + x[n] + 0.5f * x[n + 1]);
                hb[n >> 1] += a; y2 = y1; y1 = a;
            }
        } else if (th < 0.25 * 3.14159265358979323846) {     // below fs/8: input once per four samples, [1/2 1 1 1 1/2]
            const float bq = b * cosf(0.5f * th) / (cosf(th) * cosf(0.5f * th) * cosf(0.5f * th));
            for (int n = 0; n < RINGS_BLOCK; ++n) {
                float a = c1 * y1 - y2;
                if ((n & 3) == 1)
                    a += bq * (0.5f * (n == 1 ? last_x : x[n - 2]) + x[n - 1] + x[n] + x[n + 1] + 0.5f * x[n + 2]);
                y[n] += a; y2 = y1; y1 = a;
            }
        } else {
            for (int n = 0; n < RINGS_BLOCK; n += 2) {
                const float a = c1 * y1 - y2 + b * (x[n] + x[n + 1]);
                const float z = c1 * a - y1;
                y[n] += a; y[n + 1] += z; y2 = a; y1 = z;
            }
        }
        const float d = powf(r, (float)RINGS_BLOCK);
        v->ry1[k] = y1 * d; v->ry2[k] = y2 * d;
        if (strike) {
            float gr, gi; strike_g(f / f0n, &gr, &gi);
            const float w = hr ? 2.f * th : th, c = cosf(w), sn = sinf(w);
            const float t1 = (hr ? s2 : s1) * d, t0 = (hr ? s12 : s2) * d, im = (t0 - c * t1) / sn;
            const float r1 = gr * t1 - gi * im, r0 = c * r1 + gr * (t0 - c * t1) + gi * sn * t1;
            v->ry1[k] += r1; v->ry2[k] += r0;
        }
    }
    last_x = x[RINGS_BLOCK - 1];
    // The mix, 8 samples late, with the half-rate modes interpolated (half-band Lagrange, 8 taps) in between
    double hs[7 + RINGS_BLOCK / 2], now[RINGS_BLOCK];
    for (int i = 0; i < 7; ++i) hs[i] = hh[i];
    for (int i = 0; i < RINGS_BLOCK / 2; ++i) hs[7 + i] = hb[i];
    for (int n = 0; n < RINGS_BLOCK; ++n) now[n] = dry * x[n] + mix * y[n];
    for (int m = 0; m < RINGS_BLOCK / 2; ++m) {
        const double* p = hs + m;
        const double e = p[3], o = (1225.0 * (p[3] + p[4]) - 245.0 * (p[2] + p[5]) + 49.0 * (p[1] + p[6]) - 5.0 * (p[0] + p[7])) / 2048.0;
        const int n = 2 * m;
        x[n] = (float)((n < 8 ? late[n] : now[n - 8]) + mix * e);
        x[n + 1] = (float)((n + 1 < 8 ? late[n + 1] : now[n - 7]) + mix * o);
    }
    for (int n = 0; n < 8; ++n) late[n] = now[RINGS_BLOCK - 8 + n];
    for (int i = 0; i < 7; ++i) hh[i] = hs[RINGS_BLOCK / 2 + i];
}
// The machine's whole block (engine.cpp) on the reference's state, in double precision, with its stages in the
// port's units. fir holds the low-pass FIR's last two inputs.
static void block_lite(rings_t* v, float* fir, double* tap[4], int at)
{
    float x[RINGS_BLOCK];
    core_block_1x(v, x);
    for (int n = 0; n < RINGS_BLOCK; ++n) tap[0][at + n] = x[n];
    float e1, o1; adsr_block(v, &e1, &o1);
    const float e0 = v->ex_prev, o0 = v->eo_prev; v->ex_prev = e1; v->eo_prev = o1;
    const float gs = DIST_PAD * G_FM, h = rings6::LP_FIR / 2147483648.0;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const float g = e0 + (e1 - e0) * (float)(n + 1) / RINGS_BLOCK;
        x[n] *= g;
        tap[1][at + n] = x[n];
        x[n] *= gs;
    }
    // The chain (engine.cpp): its high-pass shapes the drive, unnormalised (x D, the band carries 1 / D), the band
    // follows the modes
    static rings_biquad st1, st2; static int ch_note = -1;
    if (ch_note != v->note || at == 0) {
        const float u = tanf(3.14159265358979323846f * (float)note_hz(v->note) / v->fs), D = 1.f + CH_A1 * u + CH_A0 * u * u;
        const float z[4] = {st1.z1, st1.z2, st2.z1, st2.z2};
        bilinear(&st1, 1.f, 0.f, 0.f, CH_A1, CH_A0, u); bilinear(&st2, CH_B2, CH_B1, 0.f, CH_C1, CH_C0, u);
        st1.b0 *= D; st1.b1 *= D; st1.b2 *= D; st2.b0 /= D; st2.b1 /= D; st2.b2 /= D;
        st1.z1 = z[0]; st1.z2 = z[1]; st2.z1 = z[2]; st2.z2 = z[3];
        if (at == 0) st1.z1 = st1.z2 = st2.z1 = st2.z2 = 0.f;
        ch_note = v->note;
    }
    for (int n = 0; n < RINGS_BLOCK; ++n) x[n] = bq_run(&st1, x[n]);
    modal_block_lite(v, x);
    for (int n = 0; n < RINGS_BLOCK; ++n) tap[2][at + n] = x[n] / ((1.f - WET) * gs);
    const float gsx = DIST_PAD * G_FX * v->outgain;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const float g = o0 + (o1 - o0) * (float)(n + 1) / RINGS_BLOCK;
        const float in = x[n] * gsx;
        const float lp = fir[1] + h * (fir[0] + in - 2.f * fir[1]);
        fir[0] = fir[1]; fir[1] = in;
        const double o = bq_run(&st2, lp * g);       // the onset's second gain before the band
        tap[3][at + n] = o > 0.5 ? 0.5 : (o < -0.5 ? -0.5 : o);   // the ceiling
    }
}
#undef float
#undef expf
#undef powf
#undef sqrtf
#undef exp2f
#undef log2f
#undef fabsf
#undef floorf
#undef cosf
#undef sinf
#undef tanf
#undef rings_init
#undef rings_set_knob
#undef rings_set_gain
#undef rings_note_on
#undef rings_note_off
#undef rings_process
#undef rings_active
}
#pragma GCC diagnostic pop

namespace {

const double GAIN = 143.0 * 3.9810717055349722;   // the machine's output gain against the reference page's: 143, +12 dB
const opxy::i32 DECLARED_PEAK = 1073741823;  // definition.json outputPeakQ31 (half scale; the engine clips there)

struct Setting { const char* name; int note; double drop, ratio, stretch, modes; };
// port-kit/reference/manifest.json. The envelope renders (attack, decay, sustain) are the defaults here: the stock
// amp envelope plays those parts on the device. tools/check_renders.py compares them with the envelope applied.
const double D_DROP = 0.0, D_RATIO = 0.08855468749999998, D_STRETCH = 1.0, D_MODES = 0.14269531250000003;
const Setting SETTINGS[] = {
    {"default_C2", 36, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"default_C3", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"default_C4", 60, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"default_C5", 72, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"drop_025", 48, 0.25, D_RATIO, D_STRETCH, D_MODES},
    {"drop_053", 48, 0.533, D_RATIO, D_STRETCH, D_MODES},
    {"drop_075", 48, 0.75, D_RATIO, D_STRETCH, D_MODES},
    {"drop_100", 48, 1.0, D_RATIO, D_STRETCH, D_MODES},
    {"ratio_000", 48, D_DROP, 0.0, D_STRETCH, D_MODES},
    {"ratio_025", 48, D_DROP, 0.25, D_STRETCH, D_MODES},
    {"ratio_050", 48, D_DROP, 0.5, D_STRETCH, D_MODES},
    {"ratio_100", 48, D_DROP, 1.0, D_STRETCH, D_MODES},
    {"stretch_000", 48, D_DROP, D_RATIO, 0.0, D_MODES},
    {"stretch_025", 48, D_DROP, D_RATIO, 0.25, D_MODES},
    {"stretch_050", 48, D_DROP, D_RATIO, 0.5, D_MODES},
    {"stretch_075", 48, D_DROP, D_RATIO, 0.75, D_MODES},
    {"modes_000", 48, D_DROP, D_RATIO, D_STRETCH, 0.0},
    {"modes_021", 48, D_DROP, D_RATIO, D_STRETCH, 0.2143},
    {"modes_050", 48, D_DROP, D_RATIO, D_STRETCH, 0.5},
    {"modes_100", 48, D_DROP, D_RATIO, D_STRETCH, 1.0},
    {"attack_300ms", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"attack_1s", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"decay_short", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"decay_long", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"sustain_050", 48, D_DROP, D_RATIO, D_STRETCH, D_MODES},
    {"extreme_a", 84, 1.0, 1.0, 0.0, 1.0},
    {"extreme_b", 24, 0.6, 0.05, 1.0, 1.0},
};
const int BLOCKS = 3 * 44100 / 64;      // 2067 blocks: the reference's 3 s, 132288 samples
const int N = BLOCKS * 64;

enum { T_CORE, T_DRIVE, T_MIX, T_OUT, T_COUNT };


// The double-precision model of the machine (dbl::block_lite) over a setting.
opxy::i32 word(double knob) { return static_cast<opxy::i32>(std::floor(knob * 2147483647.0 + 0.5)); }
void model_run(const Setting& s, double* tap[T_COUNT]) {
    static dbl::rings_t v;
    double fir[2] = {0, 0};
    dbl::dbl_rings_init(&v, 44100.f);
    dbl::s2pp_state = 0;
    dbl::last_x = 0;
    for (int i = 0; i < 8; ++i) dbl::late[i] = 0;
    for (int i = 0; i < 7; ++i) dbl::hh[i] = 0;
    dbl::stretch_word = word(s.stretch);
    const double knobs[4] = {s.drop, s.ratio, s.stretch, s.modes};
    for (int k = 0; k < 4; ++k) dbl::dbl_rings_set_knob(&v, (dbl::rings_knob)k, knobs[k]);
    dbl::dbl_rings_set_knob(&v, dbl::RINGS_ATTACK, 0.0);
    dbl::dbl_rings_set_knob(&v, dbl::RINGS_SUSTAIN, 1.0);   // no envelope of the voice's own
    dbl::dbl_rings_set_gain(&v, GAIN);
    dbl::dbl_rings_note_on(&v, s.note);
    v.inc_prev *= 2;                                   // the drop's start at 1x
    for (int at = 0; at < N; at += RINGS_BLOCK) dbl::block_lite(&v, fir, tap, at);
}
// The float reference as it is, as the machine runs it (ATTACK 0, SUSTAIN 1), at the machine's gain.
void spec_run(const Setting& s, double* out, float* buffer) {
    static rings_t v;
    rings_init(&v, 44100.f);
    const double knobs[4] = {s.drop, s.ratio, s.stretch, s.modes};
    for (int k = 0; k < 4; ++k) rings_set_knob(&v, (rings_knob)k, knobs[k]);
    rings_set_knob(&v, RINGS_ATTACK, 0.0);
    rings_set_knob(&v, RINGS_SUSTAIN, 1.0);
    rings_set_gain(&v, (float)GAIN);
    rings_note_on(&v, s.note);
    rings_process(&v, buffer, N);
    for (int i = 0; i < N; ++i) out[i] = buffer[i];
}

double note_hz_ref(int n) { return 440.0 * std::exp2((n - 69) / 12.0) * TUNE_RATIO; }

// The port's pitch: the reference's own 2x increment, doubled, so the oscillators run identically.
opxy::u32 pitch_for(int note) {
    const opxy::u32 inc2x = static_cast<opxy::u32>(note_hz_ref(note) / 88200.0 * 4294967296.0);
    return inc2x * 2u;
}

double snr(const double* port, const double* ref, int lag) {
    double sig = 0, err = 0;
    for (int i = 0; i + lag < N; ++i) {
        const double e = port[i + lag] - ref[i];
        sig += ref[i] * ref[i];
        err += e * e;
    }
    return 10 * std::log10(sig / (err + 1e-30));
}

struct Result { double snr[T_COUNT], snr_float; opxy::i32 peak; };

Result run(const Setting& s, double* port[T_COUNT], double* exact[T_COUNT], double* spec[T_COUNT], opxy::i32* pcm) {
    static rings6::Voice voice;
    static rings6::Shared shared;
    opxy::i32 parameters[8] = {word(s.drop), word(s.ratio), word(s.stretch), word(s.modes), 0, 0, 0, 0};
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = pitch_for(s.note);
    rings6::initialize(voice, shared, 1);
    rings6::start(voice, shared, 1);
    rings6::Taps taps;
    Result r = {{0, 0, 0, 0}, 0, 0};
    for (int b = 0; b < BLOCKS; ++b) {
        rings6::process(voice, pcm + b * 64, parameters, pitch, &taps);
        const opxy::i32* t[T_COUNT] = {taps.core, taps.drive, taps.mix, taps.out};
        for (int k = 0; k < T_COUNT; ++k)
            for (int n = 0; n < 64; ++n) port[k][b * 64 + n] = t[k][n] / 2147483648.0;
        for (int n = 0; n < 64; ++n) {
            const opxy::i32 a = pcm[b * 64 + n] < 0 ? -pcm[b * 64 + n] : pcm[b * 64 + n];
            if (a > r.peak) r.peak = a;
        }
    }
    model_run(s, exact);
    static float buffer[N];
    spec_run(s, spec[T_OUT], buffer);
    for (int k = 0; k < T_COUNT; ++k) r.snr[k] = snr(port[k], exact[k], 0);
    r.snr_float = snr(port[T_OUT], spec[T_OUT], 0);
    return r;
}

void put32(FILE* f, unsigned v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; std::fwrite(b, 1, 4, f); }
void put16(FILE* f, unsigned v) { unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; std::fwrite(b, 1, 2, f); }
bool wav(const char* path, const void* data, int n, bool is_float) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite("RIFF", 1, 4, f); put32(f, 36u + 4u * (unsigned)n); std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, is_float ? 3 : 1); put16(f, 1); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 32);
    std::fwrite("data", 1, 4, f); put32(f, 4u * (unsigned)n); std::fwrite(data, 4, (size_t)n, f);
    return std::fclose(f) == 0;
}

// A held note bent continuously: the set-up runs every block. Sounds, stays bounded, no state blows up.
bool bend_check() {
    static rings6::Voice voice;
    static rings6::Shared shared;
    const opxy::i32 parameters[8] = {word(0.5), word(0.3), word(0.2), word(1.0), 0, 0, 0, 0};
    opxy::u32 pitch[64];
    opxy::i32 out[64];
    rings6::initialize(voice, shared, 1);
    rings6::start(voice, shared, 1);
    bool ok = true, sounding = false;
    for (int b = 0; b < 2000; ++b) {
        const double semis = 24.0 * std::sin(b * 0.01) - 12.0;
        const opxy::u32 inc = static_cast<opxy::u32>(note_hz_ref(48) * std::exp2(semis / 12.0) / 44100.0 * 4294967296.0);
        for (int i = 0; i < 64; ++i) pitch[i] = inc;
        if (b % 300 == 0) rings6::start(voice, shared, 1);
        rings6::render(voice, shared, 1, out, parameters, pitch);
        for (int i = 0; i < 64; ++i) {
            sounding = sounding || out[i] != 0;
            ok = ok && out[i] >= -DECLARED_PEAK && out[i] <= DECLARED_PEAK;
        }
    }
    if (!sounding) std::puts("FAIL bend: silent");
    if (!ok) std::puts("FAIL bend: above the declared peak");
    return ok && sounding;
}
}

int main(int argc, char** argv) {
    const char* outdir = argc > 1 ? argv[1] : 0;
    double* port[T_COUNT];
    double* exact[T_COUNT];
    double* spec[T_COUNT];
    for (int k = 0; k < T_COUNT; ++k) {
        port[k] = static_cast<double*>(std::calloc(N, sizeof(double)));
        exact[k] = static_cast<double*>(std::calloc(N, sizeof(double)));
        spec[k] = static_cast<double*>(std::calloc(N, sizeof(double)));
    }
    opxy::i32* pcm = static_cast<opxy::i32*>(std::calloc(N, sizeof(opxy::i32)));
    float* fref = static_cast<float*>(std::calloc(N, sizeof(float)));
    // Sample-level SNR in dB against the double-precision reference, per stage.
    const double need[T_COUNT] = {55, 55, 55, 55};
    bool ok = true;
    opxy::i32 peak = 0;
    std::printf("SNR, dB, against the reference in double precision (and its output against the float reference)\n");
    std::printf("%-13s %6s %6s %6s %6s %7s  %s\n", "setting", "core", "drive", "mix", "out", "(float)", "peak");
    for (unsigned i = 0; i < sizeof SETTINGS / sizeof SETTINGS[0]; ++i) {
        const Setting& s = SETTINGS[i];
        const Result r = run(s, port, exact, spec, pcm);
        bool pass = true;
        for (int k = 0; k < T_COUNT; ++k) pass = pass && r.snr[k] >= need[k];
        std::printf("%-13s %6.1f %6.1f %6.1f %6.1f %7.1f  %.4f%s\n", s.name, r.snr[0], r.snr[1], r.snr[2], r.snr[3],
                    r.snr_float, r.peak / 2147483648.0, pass ? "" : "  FAIL");
        ok = ok && pass;
        if (r.peak > peak) peak = r.peak;
        if (outdir) {
            char path[1024];
            std::snprintf(path, sizeof path, "%s/%s.wav", outdir, s.name);
            ok = wav(path, pcm, N, false) && ok;
            for (int n = 0; n < N; ++n) fref[n] = static_cast<float>(spec[T_OUT][n]);
            std::snprintf(path, sizeof path, "%s/%s.ref.wav", outdir, s.name);
            ok = wav(path, fref, N, true) && ok;
        }
    }
    std::printf("loudest peak %.4f of full scale (declared %.4f)\n", peak / 2147483648.0, DECLARED_PEAK / 2147483648.0);
    if (peak > DECLARED_PEAK) { std::puts("FAIL above the declared peak"); ok = false; }
    ok = bend_check() && ok;
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
