// rings' page: the six modes as circles, spaced by their ratio to the note (STRETCH spreads or squeezes them),
// lit by how much MODES feeds each; at each note a halo rings out of every fed mode and fades with that mode's ring
// time. Below, the DROP: the pitch falling from its start offset onto the note, over its time. Screen work only.
#include "display.h"
#include "display_tables.h"

namespace rings {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

const i32 MODE_Y = 92;                 // the modes' row, pixels
const i32 HALO_DONE = 8192;            // Q10 seconds: a halo older than this is gone

// Q10 fraction of a Q31 knob word.
inline i32 fraction(i32 word) { return clamp(word, 0, 0x7fffffff) >> 21; }

// 2^s for s in [-1, 1], Q10: a quadratic on the fraction, within 0.3 %.
inline i32 exp2_q10(i32 s) {
    const bool negative = s < 0;
    const i32 f = negative ? s + unit : s;     // fraction in [0, 1)
    const i32 v = unit + ((f * (672 + ((f * 352) >> 10))) >> 10);
    return negative ? v >> 1 : v;
}

// Interpolates a 17-point table over a knob word.
inline i32 knob_table(const i32* table, i32 word) {
    const i32 x = clamp(word, 0, 0x7fffffff) >> 11;   // Q20, 0 .. 2^20
    const i32 i = x >> 16, f = (x >> 6) & 1023;
    if (i >= 16) return table[16];
    return table[i] + (((table[i + 1] - table[i]) * f) >> 10);
}

void digits(char* out, u32 capacity, i32 value, int decimals, bool sign) {
    char text[8];
    u32 n = 0;
    if (sign) text[n++] = value < 0 ? '-' : '+';
    else if (value < 0) text[n++] = '-';
    u32 v = static_cast<u32>(value < 0 ? -value : value);
    char rev[6];
    u32 r = 0;
    do { rev[r++] = static_cast<char>('0' + v % 10); v /= 10; } while ((v || static_cast<int>(r) <= decimals) && r < 6);
    while (r) {
        if (static_cast<int>(r) == decimals) text[n++] = '.';
        text[n++] = rev[--r];
    }
    text[n] = 0;
    copy(out, capacity, text);
}

// The captions: DROP 0..100, RATIO 0.00..1.00, STRETCH -100..+100, MODES 1.0..6.0.
void captions(Frame& frame, const Input& input) {
    const i32 drop = (fraction(input.parameters[0]) * 100 + 512) >> 10;
    const i32 ratio = (fraction(input.parameters[1]) * 100 + 512) >> 10;
    const i32 stretch = ((fraction(input.parameters[2]) * 200 + 512) >> 10) - 100;
    const i32 modes = 10 + ((fraction(input.parameters[3]) * 50 + 512) >> 10);
    digits(frame.header[0], sizeof frame.header[0], drop, 0, false);
    digits(frame.header[1], sizeof frame.header[1], ratio, 2, false);
    digits(frame.header[2], sizeof frame.header[2], stretch, 0, stretch != 0);
    digits(frame.header[3], sizeof frame.header[3], modes, 1, false);
}

// The DROP trace: 24 points along the fall, 8 on along the note to 100 ms, and back 2 px lower (a thin ribbon,
// every point distinct). Path units: x -56..56 for 0..100 ms, 0.6 per semitone up.
void drop_trace(Frame::path_type& path, i32 word) {
    const u32 key = 1 + (static_cast<u32>(clamp(word, 0, 0x7fffffff)) >> 18);
    if (path.key == key) return;
    const i32 st = knob_table(DISPLAY_DROP_ST, word);          // Q10 semitones
    const i32 ms = knob_table(DISPLAY_DROP_MS, word);          // Q10 ms
    Point p[32];
    for (u32 i = 0; i < 24; ++i) {
        const i32 t = (((ms >> 4) * static_cast<i32>(i)) * 2849) >> 12;   // ms i / 23, Q10 ms
        p[i].x = -56 * unit + ((t * 1147) >> 10);              // 1.12 path units per ms
        p[i].y = -((((st * DISPLAY_DROP_LEFT[i]) >> 10) * 614) >> 10);   // 0.6 per semitone
    }
    for (u32 j = 0; j < 8; ++j) {
        const i32 t = ms + (((100 * unit - ms) * static_cast<i32>(j + 1)) >> 3);
        p[24 + j].x = -56 * unit + ((t * 1147) >> 10);
        p[24 + j].y = 0;
    }
    for (u32 i = 0; i < 32; ++i) {
        path.points[i] = p[i];
        path.points[63 - i].x = p[i].x;
        path.points[63 - i].y = p[i].y + unit;
    }
    path.key = key;
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.initialized = state.track = state.previous_event = 0;
    for (u32 k = 0; k < 8; ++k) state.ages[k] = HALO_DONE;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, HALO_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    if (snapshot || input.reduced_motion) {
        for (u32 k = 0; k < 8; ++k) state.ages[k] = HALO_DONE;
    } else {
        for (u32 k = 0; k < 8; ++k) state.ages[k] = clamp(state.ages[k] + elapsed, 0, HALO_DONE);
        if (input.track < 8 && input.note_serial != state.previous_event)
            for (u32 k = 0; k < 8; ++k) state.ages[k] = 0;
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // DROP
    drop_trace(frame.paths[0], input.parameters[0]);
    place(frame, 0, 240 * unit, 186 * unit, 2 * unit, 9, stroke);

    // the modes: x by log2 of the ratio, STRETCH as 2^(2 x - 1) on the material's log2 ratio (engine.cpp, set_stretch)
    const i32 st = exp2_q10(fraction(input.parameters[2]) * 2 - unit);
    const i32 modes5 = fraction(input.parameters[3]) * 5;   // Q10: mode k is fed clamp(1 + 5x - k, 0, 1)
    for (u32 k = 6; k < 8; ++k) { hide(frame, 1 + k); hide(frame, 9 + k); }   // the artwork has eight circles; six are used
    for (u32 k = 0; k < 6; ++k) {
        const i32 fed = k == 0 ? unit : clamp(modes5 - static_cast<i32>(k - 1) * unit, 0, unit);
        const i32 lrr = (DISPLAY_L_RM[k] * st) >> 10;         // Q10 octaves
        const i32 x = clamp(96 * unit + lrr * 42, 0, 290 * unit);   // 42 px per octave, the note at 96
        const i32 size = unit + 640 - static_cast<i32>(k) * 72;
        const i32 left = state.ages[k] >= HALO_DONE ? 0
            : clamp(unit - ((state.ages[k] * DISPLAY_RING_INV[k]) >> 16), 0, unit);   // Q10, 1 at the note
        const i32 lit = (fed * left) >> 10;
        place(frame, 1 + k, x, MODE_Y * unit, size, static_cast<u32>(clamp(2 + ((fed * 7) >> 10) + ((lit * 6) >> 10), 1, 15)),
              fed ? fill_and_stroke : stroke);
        if (lit > 0 && !input.reduced_motion)
            place(frame, 9 + k, x, MODE_Y * unit, size + (((2 * unit - size) * (unit - left)) >> 10),
                  static_cast<u32>(clamp((lit * 15) >> 10, 1, 15)), stroke);
        else
            hide(frame, 9 + k);
    }
}
} // namespace rings
