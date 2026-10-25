// ringsalt's own checks, compiled for the host and run by `python ../open-xy/sdk.py test --machine machines/ringsalt`:
//   - the loop's tuning: an impulse into the tuned loop alone, its fundamental found by a Goertzel scan, within 2 cents of
//     the note, from C1 to C7, across STRETCH and TONE;
//   - odd harmonics only with STRETCH at the left: the 2nd harmonic at least 30 dB under the 3rd;
//   - the whole voice stays bounded and under its declared peak across the knobs, with retriggers.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ringsalt;

static i32 word(double k) { return static_cast<i32>(std::floor(k * 2147483647.0 + 0.5)); }
static u32 inc_for(double hz) { return static_cast<u32>(hz / 44100.0 * 4294967296.0); }

// The loop's response to an impulse, after tuning it for (hz, stretch, tone): n half-rate samples.
static void impulse(double hz, double stretch, double tone, double* y, int n) {
    static Voice v; static Shared sh;
    const i32 p[8] = {0, word(0.0886), word(stretch), word(tone), 0, 0, 0, 0};
    u32 pitch[64]; for (int i = 0; i < 64; ++i) pitch[i] = inc_for(hz);
    i32 out[64];
    initialize(v, sh, 1);
    render(v, sh, 1, out, p, pitch);                 // tunes the loop
    for (unsigned i = 0; i < ALT_LINE; ++i) v.line[i] = 0;
    v.f1x = v.f1y = v.f2x = v.f2y = v.lp = 0; v.dx[0] = v.dx[1] = v.dy[0] = v.dy[1] = 0;
    i32 uin[32], h[32];
    for (int b = 0; b * 32 < n; ++b) {
        for (int m = 0; m < 32; ++m) uin[m] = (b == 0 && m == 0) ? (1 << 28) : 0;
        loop_block(v, uin, h);
        for (int m = 0; m < 32 && b * 32 + m < n; ++m) y[b * 32 + m] = h[m] / 2147483648.0;
    }
}
// |X(f)|^2 of y (half rate) by Goertzel, after skipping the first `skip` samples.
static double power(const double* y, int n, double f) {
    const double w = 2 * M_PI * f / 22050.0, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { const double s = y[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
static double peak_cents(const double* y, int n, double hz) {
    double best = -1, at = 0;
    for (double ct = -30; ct <= 30; ct += 0.25) {
        const double p = power(y, n, hz * std::exp2(ct / 1200));
        if (p > best) { best = p; at = ct; }
    }
    return at;
}

int main() {
    bool ok = true;
    const int n = 32768;
    static double y[32768];
    std::printf("tuning, cents off the note (C1 .. C7), STRETCH / TONE:\n");
    const double settings[][2] = {{0.5, 0.6}, {0.5, 1.0}, {0.5, 0.0}, {0.8, 0.6}, {1.0, 0.6}, {0.2, 0.6}, {0.0, 0.6}};
    for (const auto& s : settings) {
        std::printf("  %.1f / %.1f:", s[0], s[1]);
        for (int note = 24; note <= 96; note += 12) {
            const double hz = 440.0 * std::exp2((note - 69) / 12.0);
            impulse(hz, s[0], s[1], y, n);
            const double c = peak_cents(y + 2048, n - 2048, hz);
            std::printf(" %+5.2f", c);
            ok = ok && std::fabs(c) <= 2.0;
        }
        std::printf("\n");
    }
    // odd harmonics only at the left
    for (int note = 36; note <= 72; note += 12) {
        const double hz = 440.0 * std::exp2((note - 69) / 12.0);
        impulse(hz, 0.0, 1.0, y, n);
        const double p2 = power(y + 2048, n - 2048, 2 * hz), p3 = power(y + 2048, n - 2048, 3 * hz);
        const double d = 10 * std::log10(p3 / (p2 + 1e-30));
        std::printf("odd only, note %d: 3rd harmonic %.0f dB over the 2nd\n", note, d);
        ok = ok && d >= 30;
    }
    // bounded across the knobs, with retriggers
    i32 peak = 0, line_peak = 0;
    for (int note = 24; note <= 108; note += 12)
        for (double st = 0; st <= 1.0; st += 0.25)
            for (double tone = 0; tone <= 1.0; tone += 0.5)
                for (double ratio = 0; ratio <= 1.0; ratio += 0.5) {
                    static Voice v; static Shared sh;
                    const i32 p[8] = {word(0.5), word(ratio), word(st), word(tone), 0, 0, 0, 0};
                    u32 pitch[64]; for (int i = 0; i < 64; ++i) pitch[i] = inc_for(440.0 * std::exp2((note - 69) / 12.0));
                    i32 out[64];
                    initialize(v, sh, 1);
                    for (int b = 0; b < 700; ++b) {
                        if (b % 200 == 0) start(v, sh, 1);
                        render(v, sh, 1, out, p, pitch);
                        for (int i = 0; i < 64; ++i) { const i32 a = out[i] < 0 ? -out[i] : out[i]; if (a > peak) peak = a; }
                        for (unsigned i = 0; i < ALT_LINE; i += 7) { const i32 a = v.line[i] < 0 ? -v.line[i] : v.line[i]; if (a > line_peak) line_peak = a; }
                    }
                }
    std::printf("loudest peak %.4f of full scale (ceiling %.4f)\n", peak / 2147483648.0, HALF_SCALE / 2147483648.0);
    std::printf("the loop's loudest sample %.4f of full scale (must stay well inside: no wrap)\n", line_peak / 2147483648.0);
    ok = ok && peak <= HALF_SCALE && line_peak < (3 << 29);
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
