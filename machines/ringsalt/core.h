#pragma once
#include <opxy/audio.hpp>
#include "tables.h"

namespace ringsalt {
using opxy::i32;
using opxy::u32;

// Only sound state. The SDK owns firmware headers, slot padding and allocation; storage starts zeroed and
// initialize() clears it again whenever the voice goes to a new owner.
struct Voice {
    // lifecycle
    u32 note_pending;        // start() ran: the next render does the note-on work
    u32 setup_inc;           // the pitch (1x phase increment) the loop was tuned for; 0 before any
    u32 knobs_seen;          // knob[] holds the words the set-up was made for
    i32 knob[4];             // drop, ratio, stretch, tone as last applied
    i32 ratio_eff;           // RATIO after its magnets, Q31
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
    // the chain: the high-pass on the drive (its last two inputs and outputs), the low-pass FIR's last two inputs, the
    // band after the loop (its last two inputs, after the onset's gain, and outputs)
    i32 fir[2];
    i32 chain[8];
    i32 ch_c[6];             // the chain's coefficients (engine.cpp, setup_chain)
    i32 dly[8];              // the mix runs 8 samples late, the loop's interpolation: its last 8 samples
    i32 hist[7];             // the loop's last 7 (half-rate) samples: its interpolation reaches back 7
    // STRETCH and TONE as set
    i32 odd;                 // Q31: how far toward odd harmonics only (STRETCH left)
    i32 amount;              // Q31: how far toward stiffness (STRETCH right)
    i32 b;                   // Q31: (1 - TONE)^2, how much faster the harmonics die
    // the loop, at half rate: the line, read at n1 (and n2 for the odd harmonics) through first-order allpass
    // fractional delays, two dispersion allpasses, a one-pole low-pass carrying the loss, and the input
    u32 w;                   // write index
    i32 n1, n2;              // whole delays, samples
    i32 a1, a2;              // fractional allpass coefficients, Q31
    i32 f1x, f1y, f2x, f2y;  // their last input and output
    i32 alpha;               // dispersion allpass coefficient, Q31 (0: none)
    i32 dx[2], dy[2];        // the dispersion allpasses' last input and output
    i32 kb, kc;              // low-pass: g b and 1 - b, Q31 (g the loss per pass)
    i32 lp;                  // its last output
    i32 ig;                  // drive into the loop, Q31
    i32 sg;                  // STRIKE into the loop, Q31
    i32 line[ALT_LINE];
};
// ringsalt voices share nothing.
struct Shared {};
}
