// metal's page: the six modes as rings, mirrored about the middle like the two tines of a tuning fork. What drives
// what (the browser lab can reassign each; the OP-XY uses these):
// - STRETCH: how far apart they stand (the modes' ratios to the note, spread or squeezed).
// - RATIO: how wide they are, and which is tallest: the lowest mode at 0, moving out to the highest at 1.
// - MODES: their shape (round at 1, square at 6), and which stand up: the modes it does not feed shrink to a third
//   and are drawn thinner.
// - DROP: their size at a strike: larger or smaller as the pitch starts below or above the note, settling onto it.
// On a strike the rings are a chain, like a slinky or a row of Newton's balls: the ring at one end is kicked (the
// left and right ends in turn) and the push travels along ring by ring, each wiggling on its own spring as it
// passes, bouncing back off the far end. And each ring pulses, wider and narrower and out and in, at a rate that
// rises with its mode and dies away with its ring time. Screen work only.
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
// The browser lab (never the OP-XY, which leaves parameter slots 4 to 7 at 0) tunes the motions and reassigns the
// knobs through those slots. Each setting is an 11-bit field, 0 for the value here, else 1 + a Q10 fraction; each
// assignment a 3-bit field, 0 for the one here, else the knob 1 .. 4, or 5 for none (the knob's default value; for
// the chain and the pulse, their whole amount).
//   slot 4: kick (bits 0-10), pass-along (11-21), assignments of SPACING, WIDTH, TALLEST_RING (22-30)
//   slot 5: wiggle, ring-out, assignments of SHAPE, STANDING, STRIKE_SIZE
//   slot 6: pulse, jolt, assignments of CHAIN_AMOUNT, PULSE_AMOUNT
enum Drive { SPACING, WIDTH, TALLEST_RING, SHAPE, STANDING, STRIKE_SIZE, CHAIN_AMOUNT, PULSE_AMOUNT };
const u32 KNOB_OF[8] = {3, 2, 2, 4, 4, 1, 5, 5};   // each drive's knob (1 DROP, 2 RATIO, 3 STRETCH, 4 MODES, 5 none)
const i32 KNOB_REST[4] = {0, 91, 1023, 205};       // Q10: each knob's default, for a drive with no knob
const i32 KICK = 807;                  // Q10: the kick, 600 x pixels a second (473)
const i32 PASS = 436;                  // Q10: the coupling to each neighbour, 30 x 2^(6 x) per s^2 (176)
const i32 WIGGLE = 742;                // Q10: each ring's own spring, 4 x Hz (2.9 Hz)
const i32 RINGOUT = 801;               // Q10: the chain's decay, 0.25 x 24^x s to 1/e (3.0 s)
const i32 PULSE = 512;                 // Q10: the pulse, 0.7 x wider and narrower (35 %), and 6 x pixels out and in
const i32 JOLT = 512;                  // Q10: the strike's size change, 2 x as much as DROP's pitch (1 x)
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

// A lab setting (an 11-bit field of slot `slot` at `shift`), or `fallback`. Q10.
inline i32 lab_setting(const Input& input, u32 slot, u32 shift, i32 fallback) {
    const u32 field = (static_cast<u32>(input.slots[slot]) >> shift) & 2047;
    return field ? static_cast<i32>(field) - 1 : fallback;
}
// What drives `drive`, Q10: its knob's value, its knob's default when it has none (the whole amount for the chain
// and the pulse), whichever the lab assigns.
inline i32 driven(const Input& input, Drive drive) {
    const u32 slot = 4 + drive / 3, shift = 22 + 3 * (drive % 3);
    const u32 field = (static_cast<u32>(input.slots[slot]) >> shift) & 7;
    const u32 knob = field >= 1 && field <= 5 ? field : KNOB_OF[drive];
    if (knob <= 4) return fraction(input.parameters[knob - 1]);
    if (drive >= CHAIN_AMOUNT) return unit;
    return KNOB_REST[KNOB_OF[drive] - 1];
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, SWING_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    const bool still = snapshot || input.reduced_motion;
    // the motions' settings: the lab's, or the ones above
    const i32 kick = ((600 * lab_setting(input, 4, 0, KICK)) >> 10) * driven(input, CHAIN_AMOUNT);   // Q10 px/s
    const i32 pass = (30 * exp2_up(6 * lab_setting(input, 4, 11, PASS))) >> 10;                     // per s^2
    const i32 wiggle = 4 * lab_setting(input, 5, 0, WIGGLE);                                        // Hz, Q10
    const i32 own = ((((wiggle * wiggle) >> 10) * 40426) >> 20);                                    // (2 pi f)^2
    const i32 fall = (4695 * lab_setting(input, 5, 11, RINGOUT)) >> 10;                             // x log2 24
    const i32 whole = fall > unit ? (fall + unit - 1) >> 10 : 1;
    const i32 damp = exp2_q10((whole << 10) - fall - unit) >> (whole - 1);            // 4 x 24^-x per s, Q8
    const i32 pulse = (lab_setting(input, 6, 0, PULSE) * driven(input, PULSE_AMOUNT)) >> 10;         // Q10
    const i32 jolt = 2 * lab_setting(input, 6, 11, JOLT);                                           // Q10
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
            if (++state.strikes & 1) state.speed[0] = clamp(state.speed[0] + kick, -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
            else state.speed[11] = clamp(state.speed[11] - kick, -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
        }
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // STRIKE_SIZE: the rings start 2^(0.3 x offset / 12) larger as DROP's pitch starts below the note (smaller
    // above), times the jolt, and settle over 0.4 s in the fall's own shape (the sound's takes 37 to 79 ms).
    i32 grow = unit;
    if (state.drop_age < 410 && jolt) {
        const i32 at = (state.drop_age * 2560) >> 10;                 // Q10 of the way through
        const i32 i = clamp((at * 23) >> 10, 0, 23);
        const i32 left = DISPLAY_DROP_LEFT[i];
        const i32 offset = knob_table(DISPLAY_DROP_ST, driven(input, STRIKE_SIZE) << 21);   // Q10 semitones
        const i32 octaves = -((((offset * left) >> 10) * 26) >> 10);
        grow = exp2_q10(clamp((octaves * jolt) >> 10, -unit, unit));
    }

    const bool pulses = !still && pulse > 0;
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
                push[j] = 4 * pass * pull - own * state.at[j] - ((damp * (state.speed[j] >> 2)) >> 5);   // 2 x damp
            }
            for (u32 j = 0; j < 12; ++j) {
                state.speed[j] = clamp(state.speed[j] + ((push[j] + 64) >> 7), -CHAIN_FASTEST * unit, CHAIN_FASTEST * unit);
                state.at[j] = clamp(state.at[j] + ((state.speed[j] + 64) >> 7), -CHAIN_MOST * unit, CHAIN_MOST * unit);
            }
        }
    }

    const i32 st = exp2_q10(driven(input, SPACING) * 2 - unit);         // as STRETCH on the modes' log2 ratios
    const i32 a0 = 8 * unit + driven(input, WIDTH) * 10;                // half width 8 .. 18 pixels
    const i32 peak = driven(input, TALLEST_RING) * 5;                   // the tallest mode, 0 .. 5, Q10
    const i32 square = driven(input, SHAPE);                            // round at 0, square at 1
    const i32 standing = driven(input, STANDING) * 5;                   // Q10: mode k stands clamp(1 + 5x - k, 0, 1)
    const i32 low = (DISPLAY_L_RM[0] * st) >> 10;
    for (u32 k = 0; k < 6; ++k) {
        const i32 fed = k == 0 ? unit : clamp(standing - static_cast<i32>(k - 1) * unit, 0, unit);
        const i32 width = 768 + ((666 * fed) >> 10);                    // stroke 0.75 .. 1.4 px
        const i32 octaves = ((DISPLAY_L_RM[k] * st) >> 10) - low;       // Q10, above the lowest mode
        const i32 out = 15 * unit + octaves * 36;                       // from the middle: 36 px an octave
        // The pulse: 2.5 Hz for the lowest mode, x 2^(0.35 octaves) above it, at most 9 Hz; it dies away as the
        // mode's ring on screen (DISPLAY_RING) does, squared.
        const i32 hz = clamp((2560 * exp2_up((octaves * 358) >> 10)) >> 10, 0, 9 * unit);
        if (pulses) state.phases[k] += (static_cast<u32>(hz) * static_cast<u32>(step)) << 12;
        const i32 left = clamp(unit - ((state.ages[k] * DISPLAY_RING_INV[k]) >> 16), 0, unit);
        const i32 swing = pulses ? (((left * left) >> 10) * sine(static_cast<i32>(((state.phases[k] >> 20) * 3600) >> 12))) >> 10 : 0;
        const i32 sway = (swing * pulse) >> 10;                         // Q10, -1 .. 1 at the most
        // Its height: 1 - (d / 2.5)^2 of the way from 0.22 to 1 of TALL, d its distance in modes from the tallest;
        // then 0.3 .. 1 of that as it stands, and the strike's growth.
        const i32 d = ((static_cast<i32>(k) * unit - peak) * 410) >> 10;
        const i32 bump = clamp(unit - ((d * d) >> 10), 0, unit);
        const i32 shape = 225 + ((799 * bump) >> 10);
        const i32 size = (shape * (307 + ((717 * fed) >> 10))) >> 10;
        const i32 a = a0 + (((a0 >> 4) * ((717 * sway) >> 10)) >> 6);  // up to 0.7 x wider and narrower
        // never shorter than it is wide: a ring shrinks to a circle (a square at MODES 6), not a dash
        const i32 b = clamp((((TALL * size) >> 4) * grow) >> 6, a, TALLEST * unit);
        const i32 r = clamp((((a < b ? a : b) >> 4) * (unit - square)) >> 6, 0, 16 * unit);
        const i32 x = out + 6 * sway;                                   // the tines: out and in, up to 6 px
        // the chain runs left to right: the left rings from mode 5 in to mode 0, then the right from 0 out to 5
        const i32 left_at = state.at[5 - k], right_at = state.at[6 + k];
        ring(frame, (2 * k) * PIECES, MIDDLE_X * unit - x + left_at, MIDDLE_Y * unit, a, b, r, width);
        ring(frame, (2 * k + 1) * PIECES, MIDDLE_X * unit + x + right_at, MIDDLE_Y * unit, a, b, r, width);
    }
}
} // namespace metal
