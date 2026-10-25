/*
 * rings: one voice of the rings synth. An FM+ DYN style two-operator exciter (with a pitch drop onto each note) into an
 * eight-mode MODAL resonator, with a linked ADSR. The sound is fixed (the settings dialed in on 24 Oct 2026, 65 % wet,
 * no bend); the voice is played with eight knobs, each 0..1:
 *
 *   DROP     the FM starts above or below the note and drops onto it (amount and time on one knob)
 *   RATIO    op2 : carrier, 0 .. 1 (linear)
 *   STRETCH  partial spacing of the modes: squeezed (0) .. as the material (0.5) .. spread (1)
 *   MODES    how many modes sound, 1 .. 8, continuously: between two counts the next mode fades in
 *   ATTACK, DECAY, SUSTAIN, RELEASE   the envelope (one per voice; the ring follows it). The resonator is always
 *            struck at full strength; ATTACK fades in the result (FM and modes together)
 *
 * Clean-room implementation: formulas from measurement and analysis only; no Elektron code or data.
 * Plain C99, float (double only for knob mapping), no allocation, block-based (16 samples), 44.1 or 48 kHz.
 */
#ifndef RINGS_H
#define RINGS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RINGS_BLOCK 16

typedef enum {
    RINGS_DROP = 0, RINGS_RATIO, RINGS_STRETCH, RINGS_MODES,
    RINGS_ATTACK, RINGS_DECAY, RINGS_SUSTAIN, RINGS_RELEASE,
    RINGS_NKNOBS
} rings_knob;

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } rings_biquad;

typedef struct {
    float fs, fsi, rs;             /* output rate, internal (2x) rate, 44100/fs */
    double knob[RINGS_NKNOBS];     /* the knobs as set, 0..1 */

    /* what the knobs set (engine units) */
    float frq2;                    /* RATIO as the FM+ DYN 2FRQ value (ratio = 4*(frq2/128)^2) */
    float sweep, sweept;           /* DROP: semitones the FM starts away from the note, seconds to get there */
    float stretch;                 /* STRETCH: -1..1 */
    float modes;                   /* MODES: 1..8, continuous (2.4 = two modes full, the third at 40 %) */
    float attack, decay, sustain, release;   /* seconds, seconds to -60 dB, level, seconds to -60 dB */
    float outgain;                 /* linear output gain */

    /* FM core */
    int note, gate;
    uint32_t inc_prev, ph0, ph2;   /* 2^32 per cycle; the oscillators free-run (never reset) */
    float s2_prev, lp1, lp2, lp_p;
    float hx[4], hy[4], hb[4];     /* 2x -> 1x half-band */
    float sweep_t, bite_t;         /* seconds since note-on: the drop, BITE */

    /* envelope (linked: each of the two stages applies sqrt(L), so the output follows L) */
    int stage;                     /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    float L;                       /* the envelope as set (ATTACK, DECAY, SUSTAIN, RELEASE): what you hear */
    float Lx;                      /* the resonator's drive: the same envelope with the shortest attack (3 ms), so the modes
                                      are always struck at full strength; ATTACK then fades in after the resonator */
    float ex_prev, eo_prev;        /* the two stages' gains at the end of the last block (into / after the resonator) */
    float att_u, att_g0, att_t;    /* attack: the linear level ramp, the gain it started from, seconds since note-on */
    float att_ux, att_gx0;         /* the same for the drive (3 ms) */

    /* track filters, presence peak, high shelf */
    rings_biquad hp1, lp1f, pk, hp2, lp2f, hs;

    /* modal resonator */
    float rr[8], ra[8], rt[8];     /* mode ratio (after material, stretch, spread), amplitude, ring-time multiplier */
    float rm[8], rmk[8];           /* the material's own ratios (before stretch / spread), make-up gain per mode */
    float ry1[8], ry2[8];          /* mode states */
    float rT;                      /* ring-time factor in use (1 while held, shorter after note-off; eased) */
    float rkey_stretch; int rkey_note;   /* what the mode set-up was made for */
    float rst;                     /* seconds since note-on: STRIKE */
} rings_t;

void rings_init(rings_t* v, float sample_rate);               /* knobs start at the defaults (RINGS_DEFAULTS) */
void rings_set_knob(rings_t* v, rings_knob k, double value);  /* 0..1 */
void rings_set_gain(rings_t* v, float linear);                /* output gain (the page's OUT slider: 10^(dB/20)) */
void rings_note_on(rings_t* v, int midi_note);
void rings_note_off(rings_t* v);
void rings_process(rings_t* v, float* out, int n);            /* mono, adds nothing: writes n samples, n multiple of 16 */
int  rings_active(const rings_t* v);                          /* 0 once the envelope has finished */

#ifdef __cplusplus
}
#endif
#endif
