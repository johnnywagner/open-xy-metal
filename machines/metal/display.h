#pragma once
#include <opxy/draw.hpp>

namespace metal {
// Twelve rings (the six modes, mirrored about the middle), each drawn from ten pieces: four quarter arcs, two
// straight pieces down each side, one across the top and one across the bottom; then three grey slivers and a hairline of sheen.
// No paths or text; 16 white strokes of different widths (a piece takes the width that undoes its scale).
using Frame = opxy::draw::Frame<0, 0, 168, 0, 16>;
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
};
}
