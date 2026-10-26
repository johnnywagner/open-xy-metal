#pragma once
#include <opxy/audio.hpp>

namespace rings {
using opxy::i32;
using opxy::u32;

const unsigned MODES = 6;               // the reference's first six (it has eight): CPU

// Only sound state. The SDK owns firmware headers, slot padding and allocation; storage starts zeroed and
// initialize() clears it again whenever the voice goes to a new owner.
struct Voice {
    // lifecycle
    u32 note_pending;        // start() ran: the next render does the note-on work
    u32 setup_inc;           // the pitch (1x phase increment) the note set-up was made for; 0 before any
    u32 knobs_seen;          // knob[] holds the words the set-up was made for
    i32 knob[4];             // drop, ratio, stretch, modes as last applied
    i32 linc;                // log2 of setup_inc as a voice-rate increment, Q24
    i32 sp, lsp;             // the FM lattice spacing for RATIO (Q26) and its log2
    i32 ldm[MODES];          // log2 of each material ratio's distance to that lattice (Q26; NO_LOG for none), make-up
    // DROP
    i32 sweep_oct;           // Q24 octaves the FM starts away from the note
    i32 sweep_du;            // Q31: how far through the drop 16 samples go
    u32 sweep_j;             // 16-sample steps since note-on
    i32 sweep_u;             // Q31: how far through the drop (MAX: done)
    u32 lbase_inc;           // the pitch increment lbase is for (0: none)
    i32 lbase;               // its log2, Q24
    // FM core
    u32 ph0, ph2, inc_prev;
    i32 s2p, fb, l1, l2;     // op2's last output, its feedback for the next sample, the one-poles
    i32 bite;                // Q31: BITE still to come, 1 at note-on
    // the onset: the reference's 3 ms eased attack; the stock amp envelope does the rest
    i32 gain;                // Q31 gain after the last 16 samples (sqrt of the level)
    i32 att_u, att_g0;
    u32 att_j, ramping;
    // the chain, at the voice rate: the high-pass on the drive (its last two inputs and outputs), the low-pass FIR's last two inputs, the
    // band after the modes (its last two inputs, after the onset's gain, and outputs)
    i32 fir[2];
    i32 chain[8];
    i32 hb[5];               // the half-band to the output rate: its last input and the allpasses' last outputs
    i32 ch_c[6];             // the chain's coefficients (engine.cpp, setup_chain)
    // MODAL: undamped two-pole oscillators at the voice rate, damped every step
    i32 y1[MODES], y2[MODES];
    i32 mc[MODES];           // below VR/7: 2 - 2 cos th, Q31; above: 2 cos th, Q30
    i32 lch[MODES];          // log2 cos(th / 2), Q24
    i32 b[MODES];            // drive into each mode, Q31
    i32 sn[MODES];           // sin th, Q31
    i32 lim[MODES];          // hot's limit on (A sin th / 2)^2, Q31 (engine.cpp)
    i32 edge[MODES];         // fade near 20 Hz and the top, Q31
    i32 wk[MODES];           // MODES' feed per mode, Q31
    i32 leps[MODES];         // log2 sqrt(1 + eps), Q24
    i32 lfk[MODES];          // log2 of the mode's voice-rate increment, clamped to 20 Hz .. the top, Q24
    i32 gre[MODES], gim[MODES];   // STRIKE's rotation (the filters it skips in the reference) at the mode, Q29
    i32 rc[MODES];           // cos th, Q31 (STRIKE's rotation)
    i32 lsn[MODES];          // log2 sin th, Q24
    i32 lrr[MODES];          // log2 of the mode's ratio to the note (material, STRETCH, spread), Q24
    i32 mk[MODES], mk_target[MODES];   // make-up gain, Q25
    u32 high;                // bit k: mode k is above VR/7
    u32 pair;                // bit k: mode k is below VR/4 (input once per two); otherwise every voice sample
    u32 gx_dirty;
};
// rings voices share nothing.
struct Shared {};
}
