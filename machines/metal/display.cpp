// metal's page: the six modes as rings, mirrored about the middle like the two tines of a tuning fork. STRETCH sets
// how far apart they stand (their ratios to the note, spread or squeezed). RATIO sets how wide they are and which is
// tallest: the lowest mode at 0, moving out to the highest at 1. MODES sets their shape (round at 1, square at 6) and
// shrinks the modes it does not feed, drawn thinner too. A note strikes them: they start larger or smaller as DROP's
// pitch starts below or above the note and settle onto it. Then each swings, wider and narrower and out and in, at a
// rate that rises with its mode and dies away with its ring time. And the rings are a chain, like a slinky or a row
// of Newton's balls: a strike kicks the ring at one end (the left and right ends in turn), and the push travels along
// ring by ring, each wiggling on its own spring as it passes, and bounces back off the far end. Screen work only.
#include "display.h"
#include "display_tables.h"

namespace metal {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;

const i32 MIDDLE_X = 240, MIDDLE_Y = 120;   // the rings' centre line, pixels
const i32 SWING_DONE = 8192;           // Q10 seconds: a swing older than this is over
const i32 TALL = 88;                   // the tallest ring's half height at rest, pixels
const i32 TALLEST = 95;                // half the drawing's height
// The browser lab (never the OP-XY, which leaves slots 4 to 7 at 0) tunes the chain through the unused parameter
// slots, each 0 for the setting below: slot 4 the kick, 5 the pass-along, 6 the wiggle, and slot 7 bits 2 up the
// ring-out (a 10-bit fraction, plus 1). Slot 7 bit 0 switches the rings' own swings off, bit 1 the chain.
const u32 LAB_NO_SWING = 1, LAB_NO_CHAIN = 2;
const i32 KICK = 655;                  // Q10 of the kick's range: 50 + 550 x pixels a second (402)
const i32 PASS = 512;                  // Q10: the coupling to each neighbour, 30 x 2^(6 x) per s^2 (240)
const i32 WIGGLE = 512;                // Q10: each ring's own spring, 4 x Hz (2 Hz)
const i32 RINGOUT = 614;               // Q10: the chain's decay, 0.5 x 12^x s to 1/e (2.2 s)
const i32 CHAIN_MOST = 40;             // pixels: no ring is pushed further
const i32 CHAIN_FASTEST = 1000;        // pixels a second: nor moves faster
const i32 PIECES = 10;                 // nodes per ring
// The palette: white strokes 0.375 px x 1.284^i wide, Q10 (tools/gen_panel.py). A piece drawn at scale s takes the
// widest entry within 1.133 of the width it should show divided by s, so every piece shows within 12 % of it.
const i32 WIDTHS[16] = {384, 493, 633, 813, 1044, 1340, 1721, 2209, 2837, 3643, 4677, 6005, 7711, 9901, 12713, 16323};
const i32 SMALLEST = 90;               // Q10: below this scale a piece would need a stroke wider than the palette's

// Q10 fraction of a Q31 knob word.
inline i32 fraction(i32 word) { return clamp(word, 0, 0x7fffffff) >> 21; }

// 2^s for s in [-1, 1], Q10: a quadratic on the fraction, within 0.3 %.
inline i32 exp2_q10(i32 s) {
    const bool negative = s < 0;
    const i32 f = negative ? s + unit : s;     // fraction in [0, 1)
    const i32 v = unit + ((f * (672 + ((f * 352) >> 10))) >> 10);
    return negative ? v >> 1 : v;
}

// 2^s for s in [0, 8), Q10.
inline i32 exp2_up(i32 s) { return exp2_q10(s & (unit - 1)) << (s >> 10); }

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

// The palette entry for a piece at scale s (Q10) that should show a stroke `width` wide (Q10 pixels).
inline u32 stroke_for(i32 s, i32 width) {
    const i32 most = (width * 1160) >> 10;        // 1.133 x
    u32 entry = 0;
    for (u32 i = 1; i < 16; ++i)
        if (((WIDTHS[i] * s) >> 10) <= most) entry = i;
    return entry;
}

// A straight piece of `length` (Q10 pixels) centred at (x, y); vertical, or across when `across`. The vertical
// pieces are drawn 64 pixels long, the ones across 32.
template <class F>
inline void straight(F& frame, u32 node, i32 x, i32 y, i32 length, bool across, i32 width) {
    const i32 s = clamp(across ? length >> 5 : length >> 6, SMALLEST, 2 * unit);
    place(frame, node, x, y, s, stroke_for(s, width), stroke);
    if (across) turn(frame, node, rotation(900));
}

// One ring: centre (cx, cy), half width a, half height b, corner radius r, all Q10 pixels. Its pieces in order: the
// quarter arcs top left, top right, bottom right and bottom left (drawn at radius 8, turned a quarter each), the two
// pieces down the left side, the two down the right, then the top and the bottom.
template <class F>
void ring(F& frame, u32 first, i32 cx, i32 cy, i32 a, i32 b, i32 r, i32 width) {
    const i32 s = r >> 3;                         // the arcs' scale
    const i32 ax = a - r, by = b - r;             // the corners' centres, from the ring's
    if (s < SMALLEST) {
        for (u32 i = 0; i < 4; ++i) hide(frame, first + i);
    } else {
        const u32 shade = stroke_for(s, width);
        const i32 xs[4] = {cx - ax, cx + ax, cx + ax, cx - ax}, ys[4] = {cy - by, cy - by, cy + by, cy + by};
        for (u32 i = 0; i < 4; ++i) {
            place(frame, first + i, xs[i], ys[i], s, shade, stroke);
            turn(frame, first + i, rotation(900 * static_cast<i32>(i)));
        }
    }
    const i32 side = by;                          // half each side's straight length: two pieces cover it
    for (u32 j = 0; j < 2; ++j) {
        const i32 x = j ? cx + a : cx - a;
        straight(frame, first + 4 + 2 * j, x, cy - (side >> 1), side, false, width);
        straight(frame, first + 5 + 2 * j, x, cy + (side >> 1), side, false, width);
    }
    straight(frame, first + 8, cx, cy - b, 2 * ax, true, width);
    straight(frame, first + 9, cx, cy + b, 2 * ax, true, width);
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.initialized = state.track = state.previous_event = 0;
    for (u32 k = 0; k < 6; ++k) { state.ages[k] = SWING_DONE; state.phases[k] = 0; }
    state.drop_age = SWING_DONE;
    for (u32 j = 0; j < 12; ++j) state.at[j] = state.speed[j] = 0;
    state.carry = 0;
    state.strikes = 0;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, SWING_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    const bool still = snapshot || input.reduced_motion;
    const u32 lab = static_cast<u32>(input.slots[7]);
    // the chain's settings: the lab's, or the ones above
    const auto lab_slot = [](i32 word, i32 fallback) { return word > 0 ? fraction(word) : fallback; };
    const i32 kick = (50 + ((550 * lab_slot(input.slots[4], KICK)) >> 10)) * unit;
    const i32 pass = (30 * exp2_up(6 * lab_slot(input.slots[5], PASS))) >> 10;        // per s^2
    const i32 wiggle = 4 * lab_slot(input.slots[6], WIGGLE);                          // Hz, Q10
    const i32 own = ((((wiggle * wiggle) >> 10) * 40426) >> 20);                      // (2 pi f)^2, per s^2
    const i32 ringout = (lab >> 2) & 2047 ? static_cast<i32>(((lab >> 2) & 2047) - 1) : RINGOUT;
    const i32 fall = (3671 * ringout) >> 10;                                         // x log2 12, Q10
    const i32 whole = fall > unit ? (fall + unit - 1) >> 10 : 1;
    const i32 damp = exp2_q10((whole << 10) - fall - unit) >> (whole - 1);            // 4 x 12^-x per s, Q8
    if (still) {
        for (u32 k = 0; k < 6; ++k) state.ages[k] = SWING_DONE;
        state.drop_age = SWING_DONE;
        for (u32 j = 0; j < 12; ++j) state.at[j] = state.speed[j] = 0;
        state.carry = 0;
    } else {
        for (u32 k = 0; k < 6; ++k) state.ages[k] = clamp(state.ages[k] + elapsed, 0, SWING_DONE);
        state.drop_age = clamp(state.drop_age + elapsed, 0, SWING_DONE);
        if (input.track < 8 && input.note_serial != state.previous_event) {
            for (u32 k = 0; k < 6; ++k) { state.ages[k] = 0; state.phases[k] = 0; }
            state.drop_age = 0;
            if (!(lab & LAB_NO_CHAIN)) {
                if (++state.strikes & 1) state.speed[0] = clamp(state.speed[0] + kick, -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
                else state.speed[11] = clamp(state.speed[11] - kick, -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
            }
        }
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // DROP: the rings start 2^(0.3 x offset / 12) larger as the pitch starts below the note (smaller above) and settle
    // over 0.4 s in the fall's own shape (the sound's fall takes 37 to 79 ms; slowed to be seen).
    i32 grow = unit;
    if (state.drop_age < 410) {
        const i32 at = (state.drop_age * 2560) >> 10;                 // Q10 of the way through
        const i32 i = clamp((at * 23) >> 10, 0, 23);
        const i32 left = DISPLAY_DROP_LEFT[i];
        const i32 offset = knob_table(DISPLAY_DROP_ST, input.parameters[0]);   // Q10 semitones
        grow = exp2_q10(clamp(-((((offset * left) >> 10) * 26) >> 10), -unit, unit));
    }

    const bool swings = !still && !(lab & LAB_NO_SWING);
    const i32 step = clamp(elapsed, 0, 205);                            // a late frame moves at most 0.2 s on

    // The chain, stepped 1/128 s at a time: each ring is pulled back to its place by its own spring and towards its
    // neighbours by the coupling (the ends by walls, so the push bounces back), and slowed by the ring-out.
    if (!still) {
        state.carry += step;
        const i32 steps = state.carry >> 3;
        state.carry &= 7;
        for (i32 n = 0; n < steps; ++n) {
            i32 push[12];
            for (u32 j = 0; j < 12; ++j) {
                const i32 before = j ? state.at[j - 1] : 0, after = j < 11 ? state.at[j + 1] : 0;
                const i32 pull = (before >> 2) - (state.at[j] >> 1) + (after >> 2);  // a quarter of the Laplacian
                push[j] = 4 * pass * pull - own * state.at[j] - ((damp * state.speed[j]) >> 8);
            }
            for (u32 j = 0; j < 12; ++j) {
                state.speed[j] = clamp(state.speed[j] + ((push[j] + 64) >> 7), -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
                state.at[j] = clamp(state.at[j] + ((state.speed[j] + 64) >> 7), -CHAIN_MOST * unit, CHAIN_MOST * unit);
            }
        }
    }

    const i32 st = exp2_q10(fraction(input.parameters[2]) * 2 - unit);   // STRETCH, as on the modes' log2 ratios
    const i32 ratio = fraction(input.parameters[1]);
    const i32 a0 = 8 * unit + ratio * 10;                               // RATIO: half width 8 .. 18 pixels
    const i32 peak = ratio * 5;                                         // RATIO: the tallest mode, 0 .. 5, Q10
    const i32 square = fraction(input.parameters[3]);                   // MODES: round at 1, square at 6
    const i32 modes5 = square * 5;                                      // Q10: mode k is fed clamp(1 + 5x - k, 0, 1)
    const i32 low = (DISPLAY_L_RM[0] * st) >> 10;
    for (u32 k = 0; k < 6; ++k) {
        const i32 fed = k == 0 ? unit : clamp(modes5 - static_cast<i32>(k - 1) * unit, 0, unit);
        const i32 width = 768 + ((666 * fed) >> 10);                    // stroke 0.75 .. 1.4 px
        const i32 octaves = ((DISPLAY_L_RM[k] * st) >> 10) - low;       // Q10, above the lowest mode
        const i32 out = 15 * unit + octaves * 36;                       // from the middle: 36 px an octave
        // The swing: 2.5 Hz for the lowest mode, x 2^(0.35 octaves) above it, at most 9 Hz; it dies away as the
        // mode's ring on screen (DISPLAY_RING) does, squared.
        const i32 hz = clamp((2560 * exp2_up((octaves * 358) >> 10)) >> 10, 0, 9 * unit);
        if (swings) state.phases[k] += (static_cast<u32>(hz) * static_cast<u32>(step)) << 12;
        const i32 left = clamp(unit - ((state.ages[k] * DISPLAY_RING_INV[k]) >> 16), 0, unit);
        const i32 swing = swings ? (((left * left) >> 10) * sine(static_cast<i32>(((state.phases[k] >> 20) * 3600) >> 12))) >> 10 : 0;
        // Its height: 1 - (d / 2.5)^2 of the way from 0.22 to 1 of TALL, d its distance in modes from RATIO's
        // tallest; then 0.3 .. 1 of that as MODES feeds it, and DROP's growth at the strike.
        const i32 d = ((static_cast<i32>(k) * unit - peak) * 410) >> 10;
        const i32 bump = clamp(unit - ((d * d) >> 10), 0, unit);
        const i32 shape = 225 + ((799 * bump) >> 10);
        const i32 size = (shape * (307 + ((717 * fed) >> 10))) >> 10;
        const i32 a = a0 + (((a0 >> 4) * ((358 * swing) >> 10)) >> 6);  // +/- 35 % wide
        // never shorter than it is wide: a ring shrinks to a circle (a square at MODES 6), not a dash
        const i32 b = clamp((((TALL * size) >> 4) * grow) >> 6, a, TALLEST * unit);
        const i32 r = clamp((((a < b ? a : b) >> 4) * (unit - square)) >> 6, 0, 16 * unit);
        const i32 x = out + 3 * swing;                                  // the tines: out and in by 3 px
        // the chain runs left to right: the left rings from mode 5 in to mode 0, then the right from 0 out to 5
        const i32 left_at = state.at[5 - k], right_at = state.at[6 + k];
        ring(frame, (2 * k) * PIECES, MIDDLE_X * unit - x + left_at, MIDDLE_Y * unit, a, b, r, width);
        ring(frame, (2 * k + 1) * PIECES, MIDDLE_X * unit + x + right_at, MIDDLE_Y * unit, a, b, r, width);
    }
}
} // namespace metal
