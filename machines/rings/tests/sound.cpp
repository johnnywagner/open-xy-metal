// rings' own sound checks, compiled for the host and run by `python ../open-xy/sdk.py test --machine machines/rings`.
//
// The reference engine (port-kit/engine/rings.c) is compiled into this test twice: as it is (float, the spec), and
// in double precision, the same formulas without float32 rounding and with the machine's 1x FM core (core_block_1x
// below; the reference runs its core at 2x). Every render in port-kit/reference/manifest.json
// is rendered through the Q31 port and through the reference as this machine runs it (ATTACK 0, SUSTAIN 1: on the
// device the stock amp envelope shapes the sound), and compared sample by sample at four stages: the FM core, the
// resonator's drive, the mix after the modes, and the output.
//
// The checks are against the double-precision model: it is the port's own algorithm, so it can be held to a tight
// sample-level match. The float reference's own float32 rounding (each mode's 2 r cos th, op2's increment) puts it a
// few cents and a few LSB from its formulas, and the 1x core's phase differs from the 2x core's half-band, so the SNR
// against the float reference (printed alongside) is lower; tools/check_renders.py judges that with compare.py.
//
//   sound            the checks: per-stage SNR, peak bound, a held bend with retriggers; exit 1 on failure
//   sound OUTDIR     also write OUTDIR/<name>.wav (the port, 32-bit PCM) and OUTDIR/<name>.ref.wav (the float
//                    reference as the machine runs it, float WAV, at the machine's gain), for tools/check_renders.py
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
#define rings_init dbl_rings_init
#define rings_set_knob dbl_rings_set_knob
#define rings_set_gain dbl_rings_set_gain
#define rings_note_on dbl_rings_note_on
#define rings_note_off dbl_rings_note_off
#define rings_process dbl_rings_process
#define rings_active dbl_rings_active
#include "../../../port-kit/engine/rings.c"
// The machine's FM core runs at 1x with the 2x rate's quantities (engine.cpp, core_step): the same, in double
// precision, on the reference's own state and helpers.
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
    float s2p = v->s2_prev, l1 = v->lp1, l2 = v->lp2;
    for (int k = 0; k < RINGS_BLOCK; ++k) {
        ph2 += inc2;
        const float s2 = sinu(ph2 + cyc2u(fb * s2p));
        const float d = clip1(0.5f * (s2 - s2p)); s2p = s2;
        const float m = l1; l1 = 0.25f * l1 + 0.75f * d;
        const float u = l2; l2 = 0.25f * l2 + 0.75f * clip1(depth * m);
        inc += (uint32_t)dinc;
        x[k] = 0.0625f * sinu(ph0);
        ph0 += inc + 2u * cyc2u(0.25f * u);
    }
    v->ph0 = ph0; v->ph2 = ph2; v->s2_prev = s2p; v->lp1 = l1; v->lp2 = l2; v->inc_prev = inc0;
}
#undef float
#undef expf
#undef powf
#undef sqrtf
#undef exp2f
#undef log2f
#undef fabsf
#undef floorf
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

const double GAIN = 143.0;             // the machine's output gain against the reference page's
const opxy::i32 DECLARED_PEAK = 402653184;   // definition.json outputPeakQ31 (3/16 full scale)
// The dry share of the mix at the machine's gain (the port carries it into the modes' drive).
const double DRY = (1.0 - WET) * -(DIST_PAD * G_FX) * GAIN / 8.0;

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

opxy::i32 word(double knob) { return static_cast<opxy::i32>(std::floor(knob * 2147483647.0 + 0.5)); }

// The reference, one 16-sample block, with its stages scaled to the port's units. R is the float or the
// double-precision build; its own helpers are found through its namespace.
// The port runs the reference's second fixed low-pass ahead of the modes (the chain is linear), so the reference's
// drive and mix are compared through that low-pass too: lp[0] and lp[1].
template <class R, class S, class B> void reference_block(R* v, void (*core)(R*, S*), B* lp, double* tap[T_COUNT], int at) {
    S x[RINGS_BLOCK];
    core(v, x);
    for (int n = 0; n < RINGS_BLOCK; ++n) tap[T_CORE][at + n] = 2.0 * x[n];    // the port's core is 2x x
    S e1, o1; adsr_block(v, &e1, &o1);
    const S e0 = v->ex_prev, o0 = v->eo_prev; v->ex_prev = e1; v->eo_prev = o1;
    const S gs = DIST_PAD * G_FM;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const S g = e0 + (e1 - e0) * (S)(n + 1) / RINGS_BLOCK;
        x[n] = bq_run(&v->pk, bq_run(&v->lp1f, bq_run(&v->hp1, x[n] * g * gs)));
        tap[T_DRIVE][at + n] = -8.0 * DRY * bq_run(&lp[0], x[n]);   // with the dry share; the port drops both stages' negative signs
    }
    modal_block(v, x);
    for (int n = 0; n < RINGS_BLOCK; ++n) tap[T_MIX][at + n] = DIST_PAD * G_FX * GAIN * bq_run(&lp[1], x[n]);   // output gain folded in
    const S gsx = DIST_PAD * G_FX * v->outgain;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const S g = o0 + (o1 - o0) * (S)(n + 1) / RINGS_BLOCK;
        tap[T_OUT][at + n] = bq_run(&v->hs, bq_run(&v->lp2f, bq_run(&v->hp2, x[n] * g * gsx)));
    }
}
template <class R, class S, class B, class Init, class Knob, class Gain, class On>
void reference_run(const Setting& s, void (*core)(R*, S*), bool one_x, double* tap[T_COUNT], Init init, Knob knob,
                   Gain gain, On on) {
    static R v;
    B lp[2];
    for (int i = 0; i < 2; ++i) bq_set(&lp[i], 0, LP_FC, LP_Q, 44100.f);
    init(&v, 44100.f);
    knob(&v, 0, s.drop); knob(&v, 1, s.ratio); knob(&v, 2, s.stretch); knob(&v, 3, s.modes);
    knob(&v, 4, 0.0);                                  // ATTACK 0
    knob(&v, 6, 1.0);                                  // SUSTAIN 1: no envelope of the voice's own
    gain(&v, GAIN);
    on(&v, s.note);
    if (one_x) v.inc_prev *= 2;                       // the drop's start at 1x
    for (int at = 0; at < N; at += RINGS_BLOCK) reference_block<R, S, B>(&v, core, lp, tap, at);
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
    static rings::Voice voice;
    static rings::Shared shared;
    opxy::i32 parameters[8] = {word(s.drop), word(s.ratio), word(s.stretch), word(s.modes), 0, 0, 0, 0};
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = pitch_for(s.note);
    rings::initialize(voice, shared, 1);
    rings::start(voice, shared, 1);
    rings::Taps taps;
    Result r = {{0, 0, 0, 0}, 0, 0};
    for (int b = 0; b < BLOCKS; ++b) {
        rings::process(voice, pcm + b * 64, parameters, pitch, &taps);
        const opxy::i32* t[T_COUNT] = {taps.core, taps.drive, taps.mix, taps.out};
        for (int k = 0; k < T_COUNT; ++k)
            for (int n = 0; n < 64; ++n) port[k][b * 64 + n] = t[k][n] / 2147483648.0;
        for (int n = 0; n < 64; ++n) {
            const opxy::i32 a = pcm[b * 64 + n] < 0 ? -pcm[b * 64 + n] : pcm[b * 64 + n];
            if (a > r.peak) r.peak = a;
        }
    }
    reference_run<dbl::rings_t, double, dbl::rings_biquad>(s, dbl::core_block_1x, true, exact, dbl::dbl_rings_init,
        [](dbl::rings_t* v, int k, double x) { dbl::dbl_rings_set_knob(v, (dbl::rings_knob)k, x); },
        [](dbl::rings_t* v, double g) { dbl::dbl_rings_set_gain(v, g); }, dbl::dbl_rings_note_on);
    reference_run<rings_t, float, rings_biquad>(s, core_block, false, spec, rings_init,
        [](rings_t* v, int k, double x) { rings_set_knob(v, (rings_knob)k, x); },
        [](rings_t* v, double g) { rings_set_gain(v, (float)g); }, rings_note_on);
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
    static rings::Voice voice;
    static rings::Shared shared;
    const opxy::i32 parameters[8] = {word(0.5), word(0.3), word(0.2), word(1.0), 0, 0, 0, 0};
    opxy::u32 pitch[64];
    opxy::i32 out[64];
    rings::initialize(voice, shared, 1);
    rings::start(voice, shared, 1);
    bool ok = true, sounding = false;
    for (int b = 0; b < 2000; ++b) {
        const double semis = 24.0 * std::sin(b * 0.01) - 12.0;
        const opxy::u32 inc = static_cast<opxy::u32>(note_hz_ref(48) * std::exp2(semis / 12.0) / 44100.0 * 4294967296.0);
        for (int i = 0; i < 64; ++i) pitch[i] = inc;
        if (b % 300 == 0) rings::start(voice, shared, 1);
        rings::render(voice, shared, 1, out, parameters, pitch);
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
