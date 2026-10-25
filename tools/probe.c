/* probe: the float reference engine with every stage of one block exposed, for choosing fixed-point scaling and for
 * stage-by-stage comparison with the Q31 port. Includes rings.c itself, so its static helpers are the reference's.
 *
 *   cc -std=c99 -O2 -Iport-kit/engine tools/probe.c -lm -o probe
 *   ./probe outdir [note=48] [secs=3] [drop=..] ... [sustain=1]   writes outdir/{core,drive,modes,mix,out}.wav (float)
 *                                                                 and prints each stage's peak
 * Option B (the machine): pass attack=0 sustain=1, the engine with no envelope of its own. */
#include "../port-kit/engine/rings.c"
#include <stdio.h>
#include <stdlib.h>

enum { S_CORE, S_DRIVE, S_HP1, S_LP1, S_PK, S_WET, S_MIX, S_STAGE2, S_HP2, S_LP2, S_OUT, S_N };
static const char* sname[S_N] = {"core", "drive", "hp1", "lp1", "pk", "wet", "mix", "stage2", "hp2", "lp2", "out"};
static float* sbuf[S_N];
static double speak[S_N];
static float modepeak[8];
static int at;

static void rec(int s, int n, float v) { sbuf[s][at + n] = v; if (fabs(v) > speak[s]) speak[s] = fabs(v); }

static void block_probe(rings_t* v, float* out)
{
    float x[RINGS_BLOCK];
    core_block(v, x);
    for (int n = 0; n < RINGS_BLOCK; ++n) rec(S_CORE, n, x[n]);
    float e1, o1; adsr_block(v, &e1, &o1);
    const float e0 = v->ex_prev, o0 = v->eo_prev; v->ex_prev = e1; v->eo_prev = o1;
    {
        const float gs = DIST_PAD * G_FM;
        for (int n = 0; n < RINGS_BLOCK; ++n) {
            const float g = e0 + (e1 - e0) * (float)(n + 1) / RINGS_BLOCK;
            float t = x[n] * g * gs; rec(S_DRIVE, n, t);
            t = bq_run(&v->hp1, t); rec(S_HP1, n, t);
            t = bq_run(&v->lp1f, t); rec(S_LP1, n, t);
            t = bq_run(&v->pk, t); rec(S_PK, n, t);
            x[n] = t;
        }
    }
    float dryx[RINGS_BLOCK]; memcpy(dryx, x, sizeof dryx);
    modal_block(v, x);
    for (int k = 0; k < 8; ++k) { const float a = fabsf(v->ry1[k]); if (a > modepeak[k]) modepeak[k] = a; }
    for (int n = 0; n < RINGS_BLOCK; ++n) { rec(S_MIX, n, x[n]); rec(S_WET, n, (x[n] - (1.f - WET) * dryx[n]) / WET); }
    const float gs = DIST_PAD * G_FX * v->outgain;
    for (int n = 0; n < RINGS_BLOCK; ++n) {
        const float g = o0 + (o1 - o0) * (float)(n + 1) / RINGS_BLOCK;
        float t = x[n] * g * gs; rec(S_STAGE2, n, t);
        t = bq_run(&v->hp2, t); rec(S_HP2, n, t);
        t = bq_run(&v->lp2f, t); rec(S_LP2, n, t);
        t = bq_run(&v->hs, t); rec(S_OUT, n, t);
        out[n] = t;
    }
}

static void put32(FILE* f, unsigned v) { unsigned char b[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255}; fwrite(b, 1, 4, f); }
static void put16(FILE* f, unsigned v) { unsigned char b[2] = {v & 255, (v >> 8) & 255}; fwrite(b, 1, 2, f); }
static void wav(const char* path, const float* y, int n)
{
    FILE* f = fopen(path, "wb"); if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, 36u + 4u * (unsigned)n); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 3); put16(f, 1); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 32);
    fwrite("data", 1, 4, f); put32(f, 4u * (unsigned)n); fwrite(y, sizeof(float), (size_t)n, f); fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [name=value ...]\n", argv[0]); return 1; }
    static const char* names[RINGS_NKNOBS] = {"drop", "ratio", "stretch", "modes", "attack", "decay", "sustain", "release"};
    int note = 48; double secs = 3.0, gain = 100.0, tune = 1.0;
    rings_t v; rings_init(&v, 44100.f);
    for (int i = 2; i < argc; ++i) {
        char key[32]; double val;
        if (sscanf(argv[i], "%31[^=]=%lf", key, &val) != 2) { fprintf(stderr, "bad argument %s\n", argv[i]); return 1; }
        if (!strcmp(key, "note")) note = (int)val;
        else if (!strcmp(key, "secs")) secs = val;
        else if (!strcmp(key, "gain")) gain = val;
        else if (!strcmp(key, "tune")) tune = val;
        else {
            int k = 0; while (k < RINGS_NKNOBS && strcmp(key, names[k])) ++k;
            if (k == RINGS_NKNOBS) { fprintf(stderr, "unknown knob %s\n", key); return 1; }
            rings_set_knob(&v, (rings_knob)k, val);
        }
    }
    (void)tune;
    rings_set_gain(&v, (float)gain);
    const int n = (int)(secs * 44100) / RINGS_BLOCK * RINGS_BLOCK;
    for (int s = 0; s < S_N; ++s) sbuf[s] = (float*)calloc((size_t)n, sizeof(float));
    float out[RINGS_BLOCK];
    rings_note_on(&v, note);
    for (at = 0; at < n; at += RINGS_BLOCK) block_probe(&v, out);
    char path[1024];
    for (int s = 0; s < S_N; ++s) {
        snprintf(path, sizeof path, "%s/%s.wav", argv[1], sname[s]); wav(path, sbuf[s], n);
        printf("%-7s peak %.6f\n", sname[s], speak[s]);
    }
    printf("modes  ");
    for (int k = 0; k < 8; ++k) printf(" %.5f", modepeak[k]);
    printf("\n");
    return 0;
}
