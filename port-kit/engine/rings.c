/* rings.c: see rings.h. Clean-room code: formulas from measurement and analysis only.
 *
 * Signal path per 16-sample block:
 *   FM core at 2x (op2 with self-feedback phase-modulating the carrier, the pitch drop on both) -> half-band to 1x
 *   -> first envelope stage + keytracked high-pass, fixed low-pass -> PRESENCE peak at 4x the note
 *   -> MODAL: eight two-pole modes (material table, STRETCH, SPREAD, STRIKE), mixed 65 % wet with the FM
 *   -> second envelope stage + keytracked high-pass, fixed low-pass, keytracked high shelf, output gain
 */
#include "rings.h"
#include <math.h>
#include <string.h>

/* =============================== the sound (fixed) ===============================
   Everything that is not a knob, from the settings dialed in on 24 Oct 2026 (rings v64). Values are the exact float32
   numbers the prototype used. */
#define ENV2       44.7054901f      /* FM depth (FM+ DYN 2ENV), steady: depth = 4*(ENV2/128)^2 */
#define FB2        8.09626961f      /* op2 self-feedback (2FB) */
#define PRESENCE   0.858333349f     /* +10.3 dB peak at 4x the note, Q 0.8, before the resonator */
#define BITE       0.600000024f     /* x2.8 FM depth at note-on, back to normal over ~25 ms */
#define HIGHSHELF  7.69999981f      /* dB, keytracked high shelf at 6x the note, on the output */
#define RINGDAMP   0.717539072f     /* after note-off the ring is gone within ~1.8x RELEASE */
#define SWEEPC     0.640722632f     /* DROP's shape: a little slow at first, faster at the end */
#define RDECAY     0.778124988f     /* MODAL ring time: 0.1*100^RDECAY s (3.6 s) */
#define RTONE      0.837460935f     /* bright: upper modes ring on */
#define RSHAPE     0.629902363f     /* material: between bar and bell */
#define RSTRIKE    0.333730459f     /* mallet pulse at note-on */
#define MSPREAD    0.00999999978f   /* each mode detuned by its own fixed amount, up to 0.5 cents */
#define WET        0.650000036f     /* dry/wet: 65 % resonator, 35 % FM (the float the page's DRY/WET at 65 % gave) */
/* DROP: one knob drives two lanes, SWEEP (semitones the FM starts away from the note) and TIME (how long it takes).
   The knob is split at DROP_SPLIT: below it each lane goes 0 -> MID on CURVE, above it MID -> 1 on CURVE 2.
   Lane ends are the page's knob values (semitones: round((v-0.5)*144)/2; time: 5*60^v ms). */
#define DROP_SPLIT 0.53306640625
static const double SW_FROM = 0.6274609374999999, SW_TO = 0.060000000000000005, SW_CURVE = 0.641015625, SW_CURVE2 = 0.10890624999999987, SW_MID = 0.47291015625;
static const double TM_FROM = 0.49197265625000003, TM_TO = 0.6733984375000001, TM_CURVE = 0.6329296875, TM_CURVE2 = 0.6794140624999999, TM_MID = 0.66265625;
/* the knobs' starting positions */
static const double RINGS_DEFAULTS[RINGS_NKNOBS] = {
    0.0,                    /* DROP    */
    0.08855468749999998,    /* RATIO   */
    1.0,                    /* STRETCH */
    0.14269531250000003,    /* MODES   */
    0.0,                    /* ATTACK  */
    0.6193554687499998,     /* DECAY   */
    0.0,                    /* SUSTAIN */
    0.64740234375};         /* RELEASE */

/* =============================== measured constants of the original track stages =============================== */
#define TUNE_RATIO 0.999546          /* the engine runs 0.79 cents flat of A440 */
#define DIST_PAD   0.0853168f        /* DIST -16 on both tracks */
#define G_FM      (-1.5483f)
#define G_FX      (-0.7249f)
#define HP_RATIO   0.45f             /* keytracked high-pass at ~0.45 x note frequency */
#define HP_Q       0.7f
#define LP_FC      20708.f
#define LP_Q       0.2f
#define HS_RATIO   6.0f              /* high shelf corner: 6 x note frequency */
#define ATTACK_MIN 0.003f            /* the attack is never shorter than this; its first 3 ms are eased (no click) */
#define RES_GAIN   2.66f             /* MODAL level */
#define STRIKE_P0  0.012f            /* STRIKE 1: the pulse's ring starts at about the exciter's peak level */
#define PI_D       3.14159265358979323846

/* =============================== small helpers =============================== */
#define SIN_N 2048
static float s_sin[SIN_N + 1];
static int s_sin_ready = 0;
static void sin_init(void)
{
    if (s_sin_ready) return;
    for (int i = 0; i <= SIN_N; ++i) s_sin[i] = (float)sin(2.0 * PI_D * i / SIN_N);
    s_sin_ready = 1;
}
static inline float sinu(uint32_t ph)       /* sin of a 32-bit phase (2^32 = one cycle) */
{
    const uint32_t i = ph >> 21; const float f = (float)(ph & 0x1FFFFFu) * (1.f / 2097152.f);
    return s_sin[i] + (s_sin[i + 1] - s_sin[i]) * f;
}
static inline uint32_t cyc2u(float c) { return (uint32_t)(int64_t)(c * 4294967296.f); }
static inline float clip1(float x) { return x > 0.99999988f ? 0.99999988f : (x < -1.f ? -1.f : x); }
static inline float flush(float x) { return (x > -1e-20f && x < 1e-20f) ? 0.f : x; }
static double note_hz(int n) { return 440.0 * exp2((n - 69) / 12.0) * TUNE_RATIO; }

/* polyphase IIR half-band (Laurent de Soras' HIIR design formula), 4 coefficients, transition 0.068 */
static void hiir_coefs(float* out, int nbr, double tbw)
{
    double k = tan((1.0 - 2.0 * tbw) * PI_D / 4.0); k *= k;
    double kk = pow(1.0 - k * k, 0.25), e = 0.5 * (1.0 - kk) / (1.0 + kk), e4 = e * e * e * e;
    double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    int order = 2 * nbr + 1;
    for (int idx = 0; idx < nbr; ++idx) {
        int c = idx + 1; double num = 0, den = 0, t; int i, j;
        i = 0; j = 1; do { t = j * pow(q, i * (i + 1)) * sin((2 * i + 1) * c * PI_D / order); num += t; j = -j; ++i; } while (fabs(t) > 1e-100);
        i = 1; j = -1; do { t = j * pow(q, i * i) * cos(2 * i * c * PI_D / order); den += t; j = -j; ++i; } while (fabs(t) > 1e-100);
        double ww = num * pow(q, 0.25) / (den + 0.5), w2 = ww * ww;
        double x = sqrt((1 - w2 * k) * (1 - w2 / k)) / (1 + w2);
        out[idx] = (float)((1 - x) / (1 + x));
    }
}

/* RBJ biquads */
static void bq_set(rings_biquad* b, int hp, float fc, float q, float fs)
{
    double w0 = 2.0 * PI_D * fc / fs, al = sin(w0) / (2.0 * q), c = cos(w0), a0 = 1.0 + al;
    if (hp) { b->b0 = (float)((1 + c) / 2 / a0); b->b1 = (float)(-(1 + c) / a0); }
    else    { b->b0 = (float)((1 - c) / 2 / a0); b->b1 = (float)((1 - c) / a0); }
    b->b2 = b->b0; b->a1 = (float)(-2 * c / a0); b->a2 = (float)((1 - al) / a0); b->z1 = b->z2 = 0.f;
}
static void bq_redesign(rings_biquad* b, int hp, float fc, float q, float fs)   /* keeps the state */
{
    const float z1 = b->z1, z2 = b->z2; bq_set(b, hp, fc, q, fs); b->z1 = z1; b->z2 = z2;
}
static inline float bq_run(rings_biquad* b, float x)
{
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

/* keytracked filters (all keep their state when retuned) */
static void set_hp(rings_t* v)
{
    const float fc = (float)(HP_RATIO * note_hz(v->note));
    bq_redesign(&v->hp1, 1, fc, HP_Q, v->fs); bq_redesign(&v->hp2, 1, fc, HP_Q, v->fs);
}
static void set_shelf(rings_t* v)            /* RBJ high shelf, slope 1, at 6x the note */
{
    const float f0 = (float)note_hz(v->note), lim = 0.45f * v->fs;
    float fh = HS_RATIO * f0; if (fh > lim) fh = lim;
    const double A = pow(10.0, HIGHSHELF / 40.0), w0 = 2.0 * PI_D * fh / v->fs, c = cos(w0), sn = sin(w0);
    const double al = sn / 2.0 * sqrt(2.0), sq = 2.0 * sqrt(A) * al;
    const double b0 = A * ((A + 1) + (A - 1) * c + sq), b1 = -2 * A * ((A - 1) + (A + 1) * c), b2 = A * ((A + 1) + (A - 1) * c - sq);
    const double a0 = (A + 1) - (A - 1) * c + sq, a1 = 2 * ((A - 1) - (A + 1) * c), a2 = (A + 1) - (A - 1) * c - sq;
    rings_biquad* b = &v->hs;
    b->b0 = (float)(b0 / a0); b->b1 = (float)(b1 / a0); b->b2 = (float)(b2 / a0); b->a1 = (float)(a1 / a0); b->a2 = (float)(a2 / a0);
}
static void set_presence(rings_t* v)          /* RBJ peaking EQ at 4x the note, Q 0.8, PRESENCE x 12 dB */
{
    const double f0 = note_hz(v->note); double fc = 4.0 * f0; if (fc > 0.45 * v->fs) fc = 0.45 * v->fs;
    const double A = pow(10.0, 12.0 * PRESENCE / 40.0), w0 = 2.0 * PI_D * fc / v->fs;
    const double al = sin(w0) / (2.0 * 0.8), cw = cos(w0), a0 = 1.0 + al / A;
    rings_biquad* b = &v->pk;
    b->b0 = (float)((1.0 + al * A) / a0); b->b1 = (float)(-2.0 * cw / a0); b->b2 = (float)((1.0 - al * A) / a0);
    b->a1 = (float)(-2.0 * cw / a0); b->a2 = (float)((1.0 - al / A) / a0);
}

/* =============================== knobs =============================== */
static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
static double curve_exp(double c) { return pow(4.0, -(c - 0.5) * 2.0); }
static double expmap(double v, double lo, double hi) { return lo * pow(hi / lo, v); }
static double sw_st(double v)                 /* a SWEEP lane end: semitones in half steps, a small dead zone at 0 */
{
    const double s = floor((v - 0.5) * 72.0 * 2.0 + 0.5) / 2.0;
    return fabs(s) < 0.5 ? 0.0 : s;
}
static double lane_pos(double x, double mid, double c1, double c2)   /* 0..1 along a lane, split at DROP_SPLIT */
{
    const double s = DROP_SPLIT;
    x = clamp01(x);
    return x <= s ? mid * pow(x / s, curve_exp(c1)) : mid + (1.0 - mid) * pow((x - s) / (1.0 - s), curve_exp(c2));
}
void rings_set_knob(rings_t* v, rings_knob k, double x)
{
    if (k < 0 || k >= RINGS_NKNOBS) return;
    v->knob[k] = x;
    switch (k) {
    case RINGS_DROP: {
        const double a0 = sw_st(SW_FROM), a1 = sw_st(SW_TO);
        double st = a0 + (a1 - a0) * lane_pos(x, SW_MID, SW_CURVE, SW_CURVE2); if (fabs(st) < 0.05) st = 0.0;
        const double t0 = 5.0 * pow(60.0, TM_FROM), t1 = 5.0 * pow(60.0, TM_TO);
        const double ms = t0 * pow(t1 / t0, lane_pos(x, TM_MID, TM_CURVE, TM_CURVE2));
        v->sweep = (float)st; v->sweept = (float)(ms / 1000.0);
    } break;
    case RINGS_RATIO: { const double r = 128.0 * sqrt(x / 4.0); v->frq2 = (float)(r < 127.0 ? r : 127.0); } break;
    case RINGS_STRETCH: v->stretch = (float)(fabs(x - 0.5) < 0.01 ? 0.0 : (x - 0.5) * 2.0); break;
    case RINGS_MODES: v->modes = (float)(1.0 + 7.0 * clamp01(x)); break;
    case RINGS_ATTACK: v->attack = (float)expmap(x, 0.001, 4.0); break;
    case RINGS_DECAY: v->decay = (float)expmap(x, 0.05, 20.0); break;
    case RINGS_SUSTAIN: v->sustain = (float)(x * x); break;
    case RINGS_RELEASE: v->release = (float)expmap(x, 0.02, 10.0); break;
    default: break;
    }
}
void rings_set_gain(rings_t* v, float g) { v->outgain = g; }

/* =============================== envelope ===============================
   Two stages, as the original two tracks: one into the resonator, one after it. L is the envelope as set; Lx is the same
   envelope with the shortest attack (ATTACK_MIN), holding at full until L's attack is done; from there on they are equal.
   Into the resonator: sqrt(Lx), so the FM always hits the modes (and STRIKE) at full strength, however long ATTACK is.
   After it: L / sqrt(Lx), so what you hear (FM and ring alike) still follows L exactly: ATTACK fades the whole sound in.
   With ATTACK at 3 ms or less, Lx = L and both stages are sqrt(L), as before. */
static void adsr_block(rings_t* v, float* ex, float* eo)
{
    const float dt = (float)RINGS_BLOCK / v->fs;
    const float A = v->attack > 0.001f ? v->attack : 0.001f;
    const float D = v->decay > 0.005f ? v->decay : 0.005f;
    const float R = v->release > 0.005f ? v->release : 0.005f;
    const float S = v->sustain < 0.f ? 0.f : (v->sustain > 1.f ? 1.f : v->sustain);
    switch (v->stage) {
    case 1: {   /* the level L rises in a straight line from where the note found it, ATTACK long; the first ATTACK_MIN is
                   eased in (gain leaves its start with zero slope), so the exciter never hits the modes like a click */
        const float Ae = A > ATTACK_MIN ? A : ATTACK_MIN;
        v->att_u += dt / Ae; if (v->att_u > 1.f) v->att_u = 1.f;
        v->att_ux += dt / ATTACK_MIN; if (v->att_ux > 1.f) v->att_ux = 1.f;
        v->att_t += dt;
        float w = v->att_t / ATTACK_MIN; w = w >= 1.f ? 1.f : w * w * (3.f - 2.f * w);
        const float g = v->att_g0 + (sqrtf(v->att_u) - v->att_g0) * w;
        const float gx = v->att_gx0 + (sqrtf(v->att_ux) - v->att_gx0) * w;
        if (v->att_u >= 1.f && w >= 1.f) { v->L = v->Lx = 1.f; v->stage = 2; } else { v->L = g * g; v->Lx = gx * gx; }
    } break;
    case 2: v->L *= powf(10.f, -3.f * dt / D); if (v->L <= S) { v->L = S; v->stage = 3; } break;   /* -60 dB in D */
    case 3: v->L = S; break;
    case 4: v->L *= powf(10.f, -3.f * dt / R); if (v->L < 1e-7f) { v->L = 0.f; v->stage = 0; } break;
    default: v->L = 0.f;
    }
    if (v->stage != 1) v->Lx = v->L;                 /* past the attack the drive is the envelope */
    const float sx = sqrtf(v->Lx);
    *ex = sx;                                        /* into the resonator */
    *eo = v->Lx == v->L ? sx : (sx > 1e-12f ? v->L / sx : 0.f);   /* after it: ex x eo = L, what you hear */
}

/* =============================== init / notes =============================== */
void rings_init(rings_t* v, float fs)
{
    memset(v, 0, sizeof(*v));
    sin_init();
    v->fs = fs; v->fsi = 2.f * fs; v->rs = 44100.f / fs;
    hiir_coefs(v->hb, 4, 0.068);
    v->lp_p = powf(0.5f, 88200.f / v->fsi);
    bq_set(&v->lp1f, 0, LP_FC < 0.49f * fs ? LP_FC : 0.49f * fs, LP_Q, fs);
    bq_set(&v->lp2f, 0, LP_FC < 0.49f * fs ? LP_FC : 0.49f * fs, LP_Q, fs);
    for (int k = 0; k < RINGS_NKNOBS; ++k) rings_set_knob(v, (rings_knob)k, RINGS_DEFAULTS[k]);
    v->outgain = 1.f;
    v->note = 60; v->rst = 1.f; v->rkey_note = -1;
    v->inc_prev = (uint32_t)(note_hz(60) / v->fsi * 4294967296.0);
    set_hp(v); set_shelf(v); set_presence(v);
}
void rings_note_on(rings_t* v, int note)
{
    v->note = note; v->gate = 1;                           /* oscillators keep running: no phase reset */
    v->sweep_t = 0.f; v->bite_t = 0.f; v->rst = 0.f;
    set_presence(v);
    if (v->sweep != 0.f) v->inc_prev = (uint32_t)(note_hz(note) * exp2f(v->sweep / 12.f) / v->fsi * 4294967296.0);   /* start there */
    set_hp(v); set_shelf(v);
    v->stage = 1;                                          /* attack from the current level */
    v->att_u = v->L; v->att_g0 = sqrtf(v->L); v->att_t = 0.f;
    v->att_ux = v->Lx; v->att_gx0 = sqrtf(v->Lx);
}
void rings_note_off(rings_t* v) { v->gate = 0; if (v->stage) v->stage = 4; }
int  rings_active(const rings_t* v) { return v->stage != 0; }

/* =============================== FM core =============================== */
/* DROP: how much of the pitch offset is left t seconds after note-on (1 -> 0 over sweept, shaped by SWEEPC) */
static float sweep_left(const rings_t* v, float t)
{
    const float T = v->sweept > 0.001f ? v->sweept : 0.001f;
    const float u = t / T; float s = 0.f;
    if (u < 1.f) {
        const float c = SWEEPC;
        if (c < 0.5f) { const float k = 1.f + 14.f * (0.5f - c) * 2.f;      /* exponential-ish: fast start */
                        s = (expf(-k * u) - expf(-k)) / (1.f - expf(-k)); }
        else { const float e = 1.f + 3.f * (c - 0.5f) * 2.f; s = 1.f - powf(u, e); }   /* straight .. accelerating */
    }
    return s;
}
static void core_block(rings_t* v, float* x)
{
    float car[2 * RINGS_BLOCK];
    double incd = note_hz(v->note) / v->fsi;
    if (v->sweep != 0.f) {                               /* the drop onto the note (both operators, ratio kept) */
        const float s = sweep_left(v, v->sweep_t);
        incd *= exp2f(v->sweep * s / 12.f);
        if (incd > 0.2) incd = 0.2;                      /* keep op2 (up to 4x) below Nyquist */
        v->sweep_t += (float)RINGS_BLOCK / v->fs;
    }
    const uint32_t inc0 = (uint32_t)(incd * 4294967296.0);
    const float f2 = v->frq2 >= 127.f ? 1.f : v->frq2 / 128.f;
    const uint32_t inc2 = (uint32_t)(incd * 4.0 * f2 * f2 * 4294967296.0);
    const float fb = 0.5f * FB2 / 128.f;
    const float e = ENV2 / 128.f;
    float depth = 4.f * e * e; if (depth > 1.f) depth = 1.f;
    depth *= 1.f + 3.f * BITE * expf(-v->bite_t / 0.025f);   /* BITE: extra depth right after note-on */
    v->bite_t += (float)RINGS_BLOCK / v->fs;
    const float pp = v->lp_p, pq = 1.f - pp;
    uint32_t inc = v->inc_prev; const int32_t dinc = (int32_t)(inc0 - v->inc_prev) / (2 * RINGS_BLOCK);
    uint32_t ph0 = v->ph0, ph2 = v->ph2;
    float s2p = v->s2_prev, l1 = v->lp1, l2 = v->lp2;
    for (int k = 0; k < 2 * RINGS_BLOCK; ++k) {
        ph2 += inc2;
        const float s2 = sinu(ph2 + cyc2u(fb * s2p));
        const float d = clip1(s2 - s2p); s2p = s2;
        const float m = l1; l1 = pp * l1 + pq * d;
        const float u = l2; l2 = pp * l2 + pq * clip1(depth * m);
        inc += (uint32_t)dinc;
        car[k] = sinu(ph0) * 0.125f;
        ph0 += inc + cyc2u(0.25f * u);
    }
    v->ph0 = ph0; v->ph2 = ph2; v->s2_prev = s2p; v->lp1 = l1; v->lp2 = l2; v->inc_prev = inc0;
    for (int n = 0; n < RINGS_BLOCK; ++n) {              /* 2x -> 1x */
        float s1 = car[2 * n], s0 = car[2 * n + 1], t;
        t = (s0 - v->hy[0]) * v->hb[0] + v->hx[0]; v->hx[0] = s0; v->hy[0] = t; s0 = t;
        t = (s0 - v->hy[2]) * v->hb[2] + v->hx[2]; v->hx[2] = s0; v->hy[2] = t; s0 = t;
        t = (s1 - v->hy[1]) * v->hb[1] + v->hx[1]; v->hx[1] = s1; v->hy[1] = t; s1 = t;
        t = (s1 - v->hy[3]) * v->hb[3] + v->hx[3]; v->hx[3] = s1; v->hy[3] = t; s1 = t;
        x[n] = 0.25f * (s0 + s1);
    }
}

/* =============================== MODAL resonator =============================== */
/* Each mode is a two-pole resonator at  f = note x ratio, where ratio comes from the material table (morphed
   by RSHAPE), bent by STRETCH and detuned by SPREAD. Unity gain at each mode for the FM signal; STRIKE adds a short pulse at
   note-on. After note-off the ring is shortened to at most ~1.8x RELEASE (eased ~40 ms). */
static const float MODAL_TAB[4][8] = {
    {1, 2, 3, 4, 5, 6, 7, 8},                                               /* harmonic (string-like)        */
    {1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.81f, 31.87f},            /* free bar (marimba / vibes)    */
    {0.5f, 1, 1.2f, 1.5f, 2, 2.5f, 3, 4},                                   /* church bell                   */
    {1, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f}};           /* circular membrane (drum)      */
static const float MODE_HASH[8] = {0.31f, -0.83f, 0.62f, -0.17f, 0.94f, -0.55f, 0.08f, -0.71f};   /* fixed per-mode "random" */
static void modal_setup(rings_t* v)
{
    v->rkey_stretch = v->stretch; v->rkey_note = v->note;
    const float s = RSHAPE * 3.f; int i = (int)s; if (i > 2) i = 2; const float u = s - (float)i;
    const float q = 1.5f - 1.3f * RTONE, pa = 1.2f * (1.f - RTONE);  /* upper modes: shorter (q) and quieter (pa) */
    const float st = exp2f(v->stretch);
    for (int k = 0; k < 8; ++k) {
        float r = exp2f(log2f(MODAL_TAB[i][k]) * (1.f - u) + log2f(MODAL_TAB[i + 1][k]) * u);
        const float rr = r < 1.f ? 1.f : r;
        v->ra[k] = powf(rr, -pa); v->rt[k] = powf(rr, -q); v->rm[k] = r;
        if (st != 1.f) r = powf(r, st);                                   /* STRETCH */
        r *= exp2f(MSPREAD * 50.f * MODE_HASH[k] / 1200.f);               /* SPREAD */
        v->rr[k] = r;
    }
}
static void modal_block(rings_t* v, float* x)
{
    if (v->stretch != v->rkey_stretch || v->note != v->rkey_note) modal_setup(v);
    /* MODES, continuous: mode k is fed at  clamp(MODES - k, 0, 1)  (mode 0 always), so between two counts the next mode fades
       in. A mode that is no longer fed is still run until its ring has died away: turning MODES down lets it ring out. */
    float wk[8];
    for (int k = 0; k < 8; ++k) { const float w = v->modes - (float)k; wk[k] = k == 0 ? 1.f : (w > 1.f ? 1.f : (w < 0.f ? 0.f : w)); }
    /* ring time: as set while the note is held; after note-off at most ~1.8x RELEASE; eased ~40 ms */
    const float T = 0.1f * powf(100.f, RDECAY);
    float Tt = T;
    if (!(v->stage >= 1 && v->stage <= 3)) {
        const float R = v->release > 0.005f ? v->release : 0.005f;
        const float Tr = R * exp2f(3.f * (1.f - RINGDAMP)); if (Tr < Tt) Tt = Tr;
    }
    const float ft = Tt / T;
    if (v->rT <= 0.f) v->rT = ft;
    v->rT += (1.f - expf(-(float)RINGS_BLOCK / (v->fs * 0.04f))) * (ft - v->rT);
    const float mix = WET, dry = 1.f - mix;
    /* STRIKE: a raised-cosine pulse at note-on, into the modes only, always at full strength (ATTACK fades it in after) */
    float pl[RINGS_BLOCK]; int strike = 0;
    const float Wp = 0.001f * powf(0.2f, RTONE), sa = STRIKE_P0 * RSTRIKE;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const float t = v->rst + (float)n / v->fs;
        pl[n] = t < Wp ? sa * 0.5f * (1.f - (float)cos(6.2831853 * t / Wp)) : 0.f; strike |= pl[n] != 0.f;
    }
    /* where the modes sit this block */
    const float f0n = (float)note_hz(v->note), ny = 0.45f * v->fs;
    v->rst += (float)RINGS_BLOCK / v->fs;
    float fk[8], ek[8], wsum = 0.f;
    for (int k = 0; k < 8; ++k) {
        float f = f0n * v->rr[k];
        /* modes near 0 Hz or above 0.45 fs fade out instead of folding over */
        float edge = 1.f;
        if (f < 40.f) { edge = (f - 20.f) / 20.f; if (edge < 0.f) edge = 0.f; f = f < 20.f ? 20.f : f; }
        if (f > ny * 0.9f) { const float e2 = (ny - f) / (0.1f * ny); edge *= e2 < 0.f ? 0.f : e2; if (f > ny) f = ny; }
        fk[k] = f; ek[k] = edge; wsum += sqrtf(v->ra[k]) * edge * wk[k];  /* the strike is shared between the audible modes */
    }
    if (wsum < 1e-6f) wsum = 1.f;
    /* make-up gain, per mode: the FM signal's energy sits on a lattice (the note plus multiples of the FM ratio x note).
       A mode moved off that lattice (STRETCH, SPREAD) catches less of it than the same mode of the plain material;
       make up the difference (at most 18 dB either way), plus up to 12 dB for modes moved up in pitch; eased ~20 ms. */
    float mkg[8];
    {
        const float f2 = v->frq2 >= 127.f ? 1.f : v->frq2 / 128.f, rfm = 4.f * f2 * f2;
        const float fq = f0n;
        const float sp = fq * (rfm > 0.05f ? rfm : 1.f), kk = 1.f - expf(-(float)RINGS_BLOCK / (v->fs * 0.02f));
        for (int k = 0; k < 8; ++k) {
            const float bw = 6.9078f / (3.14159265f * T * v->rt[k] * v->rT) + 1.f;     /* mode bandwidth, Hz */
            float dm = fabsf(f0n * v->rm[k] - f0n); dm -= sp * floorf(dm / sp); if (dm > 0.5f * sp) dm = sp - dm;
            float da = fabsf(fk[k] - fq); da -= sp * floorf(da / sp); if (da > 0.5f * sp) da = sp - da;
            const float cm = 1.f / sqrtf(1.f + 4.f * dm * dm / (bw * bw)), ca = 1.f / sqrtf(1.f + 4.f * da * da / (bw * bw));
            float mk = cm / ca; if (mk > 8.f) mk = 8.f; if (mk < 0.125f) mk = 0.125f;
            const float up = fk[k] / (fq * v->rm[k]);                     /* moved up from the FM: it has less energy there */
            if (up > 1.f) { float t = powf(up, 0.75f); mk *= t > 4.f ? 4.f : t; }
            if (v->rmk[k] <= 0.f) v->rmk[k] = mk;
            v->rmk[k] += kk * (mk - v->rmk[k]); mkg[k] = v->rmk[k];
        }
    }
    float y[RINGS_BLOCK] = {0};
    for (int k = 0; k < 8; ++k) {
        const float f = fk[k], edge = ek[k];
        if (edge <= 0.f) { v->ry1[k] = v->ry2[k] = 0.f; continue; }
        if (wk[k] <= 0.f && v->ry1[k] == 0.f && v->ry2[k] == 0.f) continue;       /* not fed and silent */
        const float Tk = T * v->rt[k] * v->rT;
        const float r = expf(-6.9078f / (Tk * v->fs)), th = 6.2831853f * f / v->fs;
        const float c1 = 2.f * r * (float)cos(th), c2 = r * r;
        const float M = sqrtf(1.f - 2.f * r * (float)cos(2.f * th) + r * r);
        const float b = (1.f - r) * M * v->ra[k] * RES_GAIN * edge * mkg[k] * wk[k];   /* unity peak gain x weight x make-up x MODES */
        const float bi = strike ? (float)sin(th) * sqrtf(v->ra[k]) * RES_GAIN * edge * wk[k] * 2.f / (Wp * v->fs * wsum) : 0.f;
        float y1 = v->ry1[k], y2 = v->ry2[k];
        for (int n = 0; n < RINGS_BLOCK; ++n) { const float o = b * x[n] + bi * pl[n] + c1 * y1 - c2 * y2; y2 = y1; y1 = o; y[n] += o; }
        v->ry1[k] = flush(y1); v->ry2[k] = flush(y2);
    }
    for (int n = 0; n < RINGS_BLOCK; ++n) x[n] = dry * x[n] + mix * y[n];
}

/* =============================== one block =============================== */
static void block(rings_t* v, float* out)
{
    float x[RINGS_BLOCK];
    core_block(v, x);
    /* first envelope stage (the drive), keytracked high-pass, low-pass, presence */
    float e1, o1; adsr_block(v, &e1, &o1);
    const float e0 = v->ex_prev, o0 = v->eo_prev; v->ex_prev = e1; v->eo_prev = o1;
    {
        const float gs = DIST_PAD * G_FM;
        for (int n = 0; n < RINGS_BLOCK; ++n) {
            const float g = e0 + (e1 - e0) * (float)(n + 1) / RINGS_BLOCK;
            x[n] = bq_run(&v->pk, bq_run(&v->lp1f, bq_run(&v->hp1, x[n] * g * gs)));
        }
    }
    modal_block(v, x);
    /* second envelope stage (ATTACK's fade-in, and the rest of the envelope), keytracked high-pass, low-pass, high shelf,
       output gain */
    const float gs = DIST_PAD * G_FX * v->outgain;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const float g = o0 + (o1 - o0) * (float)(n + 1) / RINGS_BLOCK;
        out[n] = bq_run(&v->hs, bq_run(&v->lp2f, bq_run(&v->hp2, x[n] * g * gs)));
    }
}
void rings_process(rings_t* v, float* out, int n)
{
    for (int i = 0; i + RINGS_BLOCK <= n; i += RINGS_BLOCK) block(v, out + i);
}
