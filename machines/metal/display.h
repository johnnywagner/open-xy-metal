#pragma once
#include <opxy/draw.hpp>

namespace metal {
// Twelve rings (the six modes, mirrored about the middle), each drawn as its fill (seven capsules), then its outline
// (ten: four quarter arcs, two straight pieces down each side, one across the top and one across the bottom). Palette:
// twelve white strokes of different widths (a piece takes the width that undoes its scale), then four fill colours.
// No paths or text.
using Frame = opxy::draw::Frame<0, 0, 204, 0, 16>;
struct DisplayState {
    opxy::u32 initialized, track, previous_event;
    opxy::i32 ages[6];       // Q10 seconds since each mode was struck
    opxy::u32 phases[6];     // each mode's swing, a full cycle per 2^32
    opxy::i32 drop_age;      // Q10 seconds since the note started, for DROP
    opxy::i32 at[12];        // the chain: each ring's push from its place, left to right, Q10 pixels
    opxy::i32 speed[12];     // and its speed, Q10 pixels a second
    opxy::i32 carry;         // Q10 seconds not yet stepped (the chain steps 1/128 s at a time)
    opxy::u32 strikes;       // notes so far: odd ones kick the chain from the left, even from the right
    opxy::u32 bob[12];       // each ring's bob, a full cycle per 2^32
    opxy::u32 frames;        // frames drawn, for the fill's fade
};
}
