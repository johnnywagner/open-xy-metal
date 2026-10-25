#pragma once
#include <opxy/audio.hpp>

namespace rings {
using opxy::i32;
using opxy::u32;

const unsigned MODES = 8;

// One direct-form-I biquad's history.
struct Biquad { i32 x1, x2, y1, y2; };

// Only sound state. The SDK owns firmware headers, slot padding and allocation; storage starts zeroed and
// initialize() clears it again whenever the voice goes to a new owner.
struct Voice {
    // lifecycle
    u32 note_pending;        // start() ran: the next render does the note-on work
    u32 setup_inc;           // the pitch (1x phase increment) the note set-up was made for; 0 before any
    u32 knobs_seen;          // knob[] holds the words the set-up was made for
    i32 knob[4];             // drop, ratio, stretch, modes as last applied
    // DROP
    i32 sweep_oct;           // Q24 octaves the FM starts away from the note
    i32 sweep_du;            // Q31: how far through the drop 16 samples go
    u32 sweep_j;             // 16-sample steps since note-on
    i32 sweep_u;             // Q31: how far through the drop (MAX: done)
    // FM core
    u32 ph0, ph2, inc_prev;
    i32 s2p, s2pp, l1, l2;
    i32 bite;                // Q31: BITE still to come, 1 at note-on
    // the onset: the reference's 3 ms eased attack; the stock amp envelope does the rest
    i32 gain;                // Q31 gain after the last 16 samples (sqrt of the level)
    i32 att_u, att_g0;
    u32 att_j, ramping;
    // filters, direct form I, and the low-pass FIR's last four inputs
    Biquad hp1, pk, hp2, hs;
    i32 fir[2];
    i32 hp_c[2];             // high-passes: 2 + a1, 1 - a2
    i32 hp_lb0;              // log2 of the high-passes' b0 (they run unnormalised; the shelf carries it)
    i32 pk_c[3];             // presence band-pass: (A^2 - 1) b0, 2 + a1, 1 - a2
    i32 hs_c[5];             // shelf: b0, b1, b2, a1, a2 (b with every output gain)
    // MODAL: undamped two-pole oscillators, damped every 16 samples
    i32 y1[MODES], y2[MODES];
    i32 mc[MODES];           // below fs/7: 2 - 2 cos th, Q31; above: 2 cos th, Q30
    i32 lch[MODES];          // log2 cos(th / 2), Q24
    i32 b[MODES];            // drive into each mode, Q31
    i32 sn[MODES];           // sin th, Q31
    i32 edge[MODES];         // fade near 20 Hz and 0.45 fs, Q31
    i32 wk[MODES];           // MODES' feed per mode, Q31
    i32 leps[MODES];         // log2 sqrt(1 + eps), Q24
    i32 lfk[MODES];          // log2 of the mode's increment, clamped to 20 Hz .. 0.45 fs, Q24
    i32 lrr[MODES];          // log2 of the mode's ratio to the note (material, STRETCH, spread), Q24
    i32 mk[MODES], mk_target[MODES];   // make-up gain, Q25
    u32 high;                // bit k: mode k is above fs/7
    u32 gx_dirty;
};
// rings voices share nothing.
struct Shared {};
}
