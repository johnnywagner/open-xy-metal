#pragma once
#include <opxy/draw.hpp>

namespace rings3 {
// One DROP trace (64 points), eight partial circles and eight halos; no text; 16 greys.
using Frame = opxy::draw::Frame<1, 64, 17, 0, 16>;
struct DisplayState {
    opxy::u32 initialized, track, previous_event;
    opxy::i32 ages[8];       // Q10 seconds since each mode's halo started
};
}
