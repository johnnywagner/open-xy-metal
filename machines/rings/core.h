#pragma once
#include <opxy/audio.hpp>

namespace rings {
using opxy::i32;
using opxy::u32;

const unsigned MODES = 8;

// Only sound state. The SDK owns firmware headers, slot padding and allocation; storage starts zeroed and
// initialize() clears it again whenever the voice goes to a new owner.
struct Voice {
    // lifecycle
    u32 note_pending;        // start() ran: the next render does the note-on work
    u32 setup_inc;           // the pitch (1x phase increment) the note set-up was made for; 0 before any
    u32 knobs_seen;          // knob[] holds the words the set-up was made for
    i32 knob[4];             // drop, ratio, stretch, modes as last applied
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
    // band after the modes (its last two inputs, after the onset's gain, and outputs)
    i32 fir[2];
    i32 chain[8];
    i32 core_last;           // the core's last sample: the modes' sums by four reach back one
    i32 dly[8];              // the mix runs 8 samples late, the half-rate modes' interpolation: its last 8 samples
    i32 hist[7];             // the half-rate modes' last 7 samples (their interpolation reaches back 7)
    i32 ch_c[6];             // the chain's coefficients (engine.cpp, setup_chain)
    // MODAL: undamped two-pole oscillators, damped every 16 samples
    i32 y1[MODES], y2[MODES];
    i32 mc[MODES];           // below fs/7: 2 - 2 cos th, Q31; above: 2 cos th, Q30
    i32 mh[MODES];           // half-rate modes: 2 - 2 cos 2th = 4 sin^2 th, Q30
    i32 lch[MODES];          // log2 cos(th / 2), Q24
    i32 lcos[MODES];         // log2 cos th, Q24 (modes below fs/8)
    i32 b[MODES];            // drive into each mode, Q31
    i32 sn[MODES];           // sin th, Q31
    i32 edge[MODES];         // fade near 20 Hz and 0.45 fs, Q31
    i32 wk[MODES];           // MODES' feed per mode, Q31
    i32 leps[MODES];         // log2 sqrt(1 + eps), Q24
    i32 lfk[MODES];          // log2 of the mode's increment, clamped to 20 Hz .. 0.45 fs, Q24
    i32 gre[MODES], gim[MODES];   // STRIKE's rotation (the filters it skips in the reference) at the mode, Q29
    i32 rc[MODES], rs[MODES];   // cos and sin of the angle the mode's state steps by (th, or 2 th at half rate), Q31
    i32 lrr[MODES];          // log2 of the mode's ratio to the note (harmonic or material, STRETCH, spread), Q24
    i32 lup[MODES];          // log2 of how far STRETCH moved the mode up, Q24 (make-up)
    i32 mk[MODES], mk_target[MODES];   // make-up gain, Q25
    u32 high;                // bit k: mode k is above fs/7
    u32 quad;                // bit k: mode k is below fs/8 (input once per four samples)
    u32 half;                // bit k: mode k runs at half rate (below fs/8; chosen at note-on)
    u32 gx_dirty;
};
// rings voices share nothing.
struct Shared {};
}
