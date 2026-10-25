/* render_ref: render one rings voice (the float reference engine) to a WAV file, as the OP-XY would call it:
 * 44.1 kHz, the note starts at 0 and is never released (the device has no note-off; its own amp envelope follows).
 *
 *   cc -std=c99 -O2 -I../engine render_ref.c ../engine/rings.c -lm -o render_ref
 *   ./render_ref out.wav [note=48] [secs=3] [gain=100] [off=SECONDS] [drop=..] [ratio=..] [stretch=..] [modes=..]
 *                [attack=..] [decay=..] [sustain=..] [release=..]
 *
 * Knobs are the 0..1 positions (defaults = the dialed-in sound). gain 100 = the prototype page's OUT +40 dB.
 * off=SECONDS calls rings_note_off at that time (only to compare with the prototype; the device never does).
 * Output: mono 32-bit float WAV. Prints the peak and RMS. */
#include "rings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void put32(FILE* f, unsigned v) { unsigned char b[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255}; fwrite(b, 1, 4, f); }
static void put16(FILE* f, unsigned v) { unsigned char b[2] = {v & 255, (v >> 8) & 255}; fwrite(b, 1, 2, f); }

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s out.wav [name=value ...]\n", argv[0]); return 1; }
    static const char* names[RINGS_NKNOBS] = {"drop", "ratio", "stretch", "modes", "attack", "decay", "sustain", "release"};
    const float fs = 44100.f;
    int note = 48; double secs = 3.0, gain = 100.0, off = -1.0;
    rings_t v; rings_init(&v, fs);
    for (int i = 2; i < argc; ++i) {
        char key[32]; double val;
        if (sscanf(argv[i], "%31[^=]=%lf", key, &val) != 2) { fprintf(stderr, "bad argument %s\n", argv[i]); return 1; }
        if (!strcmp(key, "note")) note = (int)val;
        else if (!strcmp(key, "secs")) secs = val;
        else if (!strcmp(key, "gain")) gain = val;
        else if (!strcmp(key, "off")) off = val;
        else {
            int k = 0; while (k < RINGS_NKNOBS && strcmp(key, names[k])) ++k;
            if (k == RINGS_NKNOBS) { fprintf(stderr, "unknown knob %s\n", key); return 1; }
            rings_set_knob(&v, (rings_knob)k, val);
        }
    }
    rings_set_gain(&v, (float)gain);
    const int n = (int)(secs * fs) / RINGS_BLOCK * RINGS_BLOCK, offb = off < 0 ? -1 : (int)(off * fs) / RINGS_BLOCK * RINGS_BLOCK;
    float* y = (float*)malloc(sizeof(float) * (size_t)n);
    if (!y) return 1;
    rings_note_on(&v, note);
    for (int i = 0; i < n; i += RINGS_BLOCK) { if (i == offb) rings_note_off(&v); rings_process(&v, y + i, RINGS_BLOCK); }
    double pk = 0, ss = 0; for (int i = 0; i < n; ++i) { const double a = fabs(y[i]); if (a > pk) pk = a; ss += (double)y[i] * y[i]; }
    FILE* f = fopen(argv[1], "wb"); if (!f) { perror(argv[1]); return 1; }
    fwrite("RIFF", 1, 4, f); put32(f, 36u + 4u * (unsigned)n); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 3); put16(f, 1); put32(f, 44100); put32(f, 44100 * 4); put16(f, 4); put16(f, 32);
    fwrite("data", 1, 4, f); put32(f, 4u * (unsigned)n); fwrite(y, sizeof(float), (size_t)n, f); fclose(f);
    printf("%s  peak %.5f (%.1f dBFS)  rms %.5f\n", argv[1], pk, 20 * log10(pk + 1e-12), sqrt(ss / n));
    free(y);
    return 0;
}
