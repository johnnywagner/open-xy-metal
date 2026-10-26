#pragma once
#include <opxy/draw.hpp>

namespace metal {
// Twelve rings (the six modes, mirrored about the middle), each drawn from ten pieces: four quarter arcs, two
// straight pieces down each side, one across the top and one across the bottom. No paths or text; 16 white strokes
// of different widths (a piece takes the width that undoes its scale).
using Frame = opxy::draw::Frame<0, 0, 120, 0, 16>;
struct DisplayState {
    opxy::u32 initialized, track, previous_event;
    opxy::i32 ages[6];       // Q10 seconds since each mode was struck
    opxy::u32 phases[6];     // each mode's swing, a full cycle per 2^32
    opxy::i32 drop_age;      // Q10 seconds since the note started, for DROP and the sway
    opxy::u32 sway_phase;    // the whole array's sway, a full cycle per 2^32
};
}
