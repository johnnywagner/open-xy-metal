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
// rises with its mode and dies away with its ring time. And each ring can bob up and down on its own, each at its own
// rate: on every note (dying away), or all the time; how deep, on a knob or not. And the lab can fill the rings across
// a knob: in steps through the OP-XY's greys to white, or in white growing smoothly, rising from the ring's foot or
// swelling from its centre (the screen draws flat colours only, but moves and scales them smoothly). Screen work only
// (screen v14).
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
// knobs through those slots, read as one stream of bits, 31 to a slot from slot 4's lowest:
//   bits 0-79: ten settings of 8 bits (Setting below), each 0 for the value here, else 1 + 254ths of the range;
//   bits 80-119: ten assignments of 4 bits (Drive below): bits 0-2 are 0 for the knob here, else the knob 1 .. 4,
//     or 5 for none (the knob's default value; the whole amount for the chain, pulse and bob; no chrome); bit 3
//     inverts the knob;
//   bits 120-121: the bob, 0 as here, 1 all the time, 2 on notes;
//   bits 122-123: the fill, 0 as here, 1 in steps, 2 rising, 3 swelling.
enum Setting { KICK_SET, PASS_SET, WIGGLE_SET, RINGOUT_SET, PULSE_SET, JOLT_SET, BOB_DEPTH_SET, BOB_SPEED_SET,
               SPACE_DEPTH_SET, SPACE_CURVE_SET };
enum Drive { SPACING, WIDTH, TALLEST_RING, SHAPE, STANDING, STRIKE_SIZE, CHAIN_AMOUNT, PULSE_AMOUNT, BOB, FILL };
const u32 KNOB_OF[10] = {3, 2, 2, 4, 4, 1, 5, 5, 5, 5};   // each drive's knob (1 DROP, 2 RATIO, 3 STRETCH, 4 MODES, 5 none)
const i32 KNOB_REST[4] = {0, 91, 1023, 205};       // Q10: each knob's default, for a drive with no knob
const i32 KICK = 807;                  // Q10: the kick, 600 x pixels a second (473)
const i32 PASS = 436;                  // Q10: the coupling to each neighbour, 30 x 2^(6 x) per s^2 (176)
const i32 WIGGLE = 742;                // Q10: each ring's own spring, 4 x Hz (2.9 Hz)
const i32 RINGOUT = 59;                // Q10: the chain's decay, 0.25 x 24^x s to 1/e (0.3 s)
const i32 PULSE = 512;                 // Q10: the pulse, 0.7 x wider and narrower (35 %), and 6 x pixels out and in
const i32 JOLT = 512;                  // Q10: the strike's size change, 2 x as much as DROP's pitch (1 x)
const i32 BOB_DEPTH = 256;             // Q10: the bob, 48 x pixels either way (12)
const i32 BOB_SPEED = 460;             // Q10: its rate, 0.1 x 60^x Hz (0.63 Hz), each ring a little off it
const i32 SPACE_DEPTH = 512;           // Q10: how far the spacing moves with its knob, 2 x as far as STRETCH (1 x)
const i32 SPACE_CURVE = 512;           // Q10: its knob's curve, 2 x - 1: slow to start below 0, fast above (even)
const bool BOB_ON_NOTES = true;        // on each note, dying away; else all the time
const i32 BOB_FADE_INV = 410;          // Q10 per second: a note's bob dies away over 2.5 s, squared
// Each ring's own bob rate, Q10 of the speed, so no two move together.
const i32 BOB_RATE[12] = {1024, 851, 1198, 932, 1116, 788, 1260, 973, 1075, 891, 1157, 819};
const i32 CHAIN_MOST = 40;             // pixels: no ring is pushed further
const i32 CHAIN_FASTEST = 1000;        // pixels a second: nor moves faster
const i32 PIECES = 20;                 // nodes per ring: the fill's 10 capsules, then the outline's 10 (gen_panel.py)
// The palette's first twelve: white strokes 0.375 px x 1.405^i wide, Q10 (tools/gen_panel.py). A piece drawn at scale
// s takes the widest within 1.133 of the width it should show divided by s, so every piece shows within 19 % of it.
const i32 WIDTHS[12] = {384, 540, 758, 1065, 1496, 2102, 2954, 4150, 5831, 8193, 11511, 16172};
// Palette entries 12 to 15: the fill's colours, the OP-XY's dark, mid and light greys, then white.
const u32 FILL_SHADE = 12;
enum FillMode { FILL_STEPS = 1, FILL_RISES = 2, FILL_SWELLS = 3 };
const u32 FILL_MODE = FILL_STEPS;       // how the fill shows its knob
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

// The palette entry for a piece at scale s (Q10) that should show a stroke `width` wide (Q10 pixels): the widest of
// the twelve white strokes within 1.133 of it.
inline u32 stroke_for(i32 s, i32 width) {
    const i32 most = (width * 1160) >> 10;        // 1.133 x
    u32 entry = 0;
    for (u32 i = 1; i < 12; ++i)
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
    for (u32 j = 0; j < 12; ++j) state.bob[j] = static_cast<u32>(j) * 0x6ED9EBA1u;   // each ring somewhere else
}

// `width` bits of the lab's stream from bit `at` (slots 4 to 7, 31 bits each).
inline u32 lab_bits(const Input& input, u32 at, u32 width) {
    const u32 slot = 4 + at / 31, shift = at % 31;
    u32 bits = static_cast<u32>(input.slots[slot]) >> shift;
    if (shift + width > 31 && slot < 7) bits |= static_cast<u32>(input.slots[slot + 1]) << (31 - shift);
    return bits & ((1u << width) - 1);
}
// A lab setting, Q10, or `fallback`.
inline i32 lab_setting(const Input& input, Setting setting, i32 fallback) {
    const u32 field = lab_bits(input, 8 * setting, 8);
    return field ? static_cast<i32>((field - 1) * 4124) >> 10 : fallback;    // 254ths, as Q10
}
// What drives `drive`, Q10: its knob's value (inverted if the lab says), its knob's default when it has none (the
// whole amount for the chain, the pulse and the bob; no chrome), whichever the lab assigns.
inline i32 driven(const Input& input, Drive drive) {
    const u32 field = lab_bits(input, 80 + 4 * drive, 4);
    const u32 knob = (field & 7) >= 1 && (field & 7) <= 5 ? field & 7 : KNOB_OF[drive];
    if (knob <= 4) {
        const i32 value = fraction(input.parameters[knob - 1]);
        return field & 8 ? unit - 1 - value : value;
    }
    if (drive == FILL) return 0;
    if (drive >= CHAIN_AMOUNT) return unit;
    return KNOB_REST[KNOB_OF[drive] - 1];
}

// 2^e for e in [-2, 2] (Q10).
inline i32 exp2_wide(i32 e) {
    e = clamp(e, -2 * unit, 2 * unit);
    return e > unit ? exp2_q10(e - unit) << 1 : e < -unit ? exp2_q10(e + unit) >> 1 : exp2_q10(e);
}

// Integer square root.
inline u32 root(u32 v) {
    u32 r = 0;
    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
    }
    return r;
}

// A filled capsule: centre (cx, cy), width w, height h (Q10 pixels), palette colour `shade` (0: none). The artwork can
// only be scaled whole, so it holds ten capsules (nodes 0-9), each FILL_TALL[i] times as tall as wide, and the fill is
// two of them as wide as w, end to end: two whose straight sides overlap make one exact capsule, from the taller one's
// height to the two's together less one width, and each pair's reach meets the next pair's (FILL_TALL[i + 2] =
// FILL_TALL[i] + FILL_TALL[i + 1] - 1), so pairs make every height from 1.5 to 23.25 widths. Below 1.5 widths it is the
// nearest one alone (a little shorter or narrower than asked); beyond 23.25 the last pair, narrower. Wider than it is
// tall, the same turned a quarter.
const i32 FILL_TALL[10] = {1024, 1280, 1536, 1792, 2304, 3072, 4352, 6400, 9728, 15104};   // Q10
const i32 FILL_WIDE_INV[10] = {1638, 1638, 1638, 1638, 1638, 1638, 1638, 2048, 3277, 4855}; // 2^16 / its width, px
template <class F>
void capsule(F& frame, u32 first, i32 cx, i32 cy, i32 w, i32 h, u32 shade) {
    for (u32 node = 0; node < 10; ++node) hide(frame, first + node);
    const bool across = h < w;                                        // lying down: turned a quarter
    i32 wide = across ? h : w, tall = across ? w : h;
    if (!shade || wide < unit) return;
    i32 one = 0, two = -1;                                            // the capsules used, the taller first
    if (tall < (1152 * wide) >> 10) one = 0;                          // to 1.125: a disc
    else if (tall < (1280 * wide) >> 10) { one = 1; wide = (tall * 819) >> 10; }       // narrower
    else if (tall < (1408 * wide) >> 10) one = 1;                     // to 1.375: shorter
    else if (tall < (1536 * wide) >> 10) { one = 2; wide = (tall * 683) >> 10; }       // narrower
    else {
        if (tall > (23808 * (wide >> 4)) >> 6) wide = (tall * 44) >> 10;   // beyond 23.25: narrower
        one = 2;
        while (one < 9 && ((FILL_TALL[one + 1] * (wide >> 4)) >> 6) <= tall) ++one;
        two = one - 1;
    }
    const i32 c = across ? cx : cy;
    for (u32 n = 0; n < 2; ++n) {
        const i32 i = n ? two : one;
        if (i < 0) break;
        const i32 length = (FILL_TALL[i] * (wide >> 4)) >> 6;         // this capsule's own height
        // alone it is centred; of a pair the taller one meets the top (or left) end, the other the bottom
        const i32 at = two < 0 ? c : n == 0 ? c - ((tall - length) >> 1) : c + ((tall - length) >> 1);
        const i32 s = clamp((wide * FILL_WIDE_INV[i]) >> 16, 1, 2 * unit);
        const u32 node = first + static_cast<u32>(i);
        place(frame, node, across ? at : cx, across ? cy : at, s, shade, fill_and_stroke);
        if (across) turn(frame, node, rotation(900));
    }
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, SWING_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    const bool still = snapshot || input.reduced_motion;
    // the motions' settings: the lab's, or the ones above
    const u32 bob_mode = lab_bits(input, 120, 2);
    const bool on_notes = bob_mode ? bob_mode == 2 : BOB_ON_NOTES;
    const i32 bob_depth = (((48 * lab_setting(input, BOB_DEPTH_SET, BOB_DEPTH)) >> 4) * driven(input, BOB)) >> 6;  // Q10 px
    const i32 bob_speed = (102 * exp2_up((6049 * lab_setting(input, BOB_SPEED_SET, BOB_SPEED)) >> 10)) >> 10;    // Hz, Q10
    const i32 kick = ((600 * lab_setting(input, KICK_SET, KICK)) >> 10) * driven(input, CHAIN_AMOUNT);   // Q10 px/s
    const i32 pass = (30 * exp2_up(6 * lab_setting(input, PASS_SET, PASS))) >> 10;                     // per s^2
    const i32 wiggle = 4 * lab_setting(input, WIGGLE_SET, WIGGLE);                                     // Hz, Q10
    const i32 own = ((((wiggle * wiggle) >> 10) * 40426) >> 20);                                       // (2 pi f)^2
    const i32 fall = (4695 * lab_setting(input, RINGOUT_SET, RINGOUT)) >> 10;                          // x log2 24
    const i32 whole = fall > unit ? (fall + unit - 1) >> 10 : 1;
    const i32 damp = exp2_q10((whole << 10) - fall - unit) >> (whole - 1);            // 4 x 24^-x per s, Q8
    const i32 pulse = (lab_setting(input, PULSE_SET, PULSE) * driven(input, PULSE_AMOUNT)) >> 10;       // Q10
    const i32 jolt = 2 * lab_setting(input, JOLT_SET, JOLT);                                           // Q10
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
            if (on_notes) for (u32 j = 0; j < 12; ++j) state.bob[j] = 0;   // each starts from its place
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

    // The bob: each ring up and down at its own rate, on each note dying away, or all the time.
    i32 bobbed[12];
    {
        const i32 fade = on_notes ? clamp(unit - ((state.drop_age * BOB_FADE_INV) >> 10), 0, unit) : unit;
        const i32 reach = (((bob_depth >> 4) * ((fade * fade) >> 10)) >> 6);       // Q10 px
        for (u32 j = 0; j < 12; ++j) {
            if (!still && reach > 0)
                state.bob[j] += static_cast<u32>((bob_speed * BOB_RATE[j]) >> 10) * static_cast<u32>(step) << 12;
            bobbed[j] = still ? 0 : ((reach >> 4) * sine(static_cast<i32>(((state.bob[j] >> 20) * 3600) >> 12))) >> 6;
        }
    }

    // SPACING, as STRETCH on the modes' log2 ratios: its knob through the curve (cubic, slow to start or fast),
    // then 2^((2 x - 1) depth), depth 0 .. 2
    i32 spread = driven(input, SPACING);
    {
        const i32 bend = 2 * lab_setting(input, SPACE_CURVE_SET, SPACE_CURVE) - unit;      // -1 .. 1
        const i32 cube = (((spread * spread) >> 10) * spread) >> 10;
        const i32 rest = unit - spread, ease = unit - ((((rest * rest) >> 10) * rest) >> 10);
        spread += bend > 0 ? ((cube - spread) * bend) >> 10 : ((ease - spread) * -bend) >> 10;
    }
    const i32 st = exp2_wide(((2 * spread - unit) * 2 * lab_setting(input, SPACE_DEPTH_SET, SPACE_DEPTH)) >> 10);
    // FILL: in steps, none below a tenth of its knob, then a quarter of the rest each for the dark, mid and light grey
    // and white; or white, rising from the ring's foot or swelling from its centre with the knob
    const i32 shine = driven(input, FILL);
    const u32 fill_mode = lab_bits(input, 122, 2) ? lab_bits(input, 122, 2) : FILL_MODE;
    const i32 steps = shine < 102 ? 0 : clamp(1 + (((shine - 102) * 4551) >> 20), 1, 4);   // 0 none, 1 .. 4
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
        // (and room left above and below for the bob)
        const i32 b = clamp((((TALL * size) >> 4) * grow) >> 6, a, TALLEST * unit - bob_depth);
        const i32 r = clamp((((a < b ? a : b) >> 4) * (unit - square)) >> 6, 0, 16 * unit);
        const i32 x = clamp(out + 6 * sway, 0, 260 * unit);             // the tines: out and in, up to 6 px
        // the chain runs left to right: the left rings from mode 5 in to mode 0, then the right from 0 out to 5
        const i32 left_at = state.at[5 - k], right_at = state.at[6 + k];
        const i32 lx = MIDDLE_X * unit - x + left_at, rx = MIDDLE_X * unit + x + right_at;
        const i32 ly = MIDDLE_Y * unit + bobbed[5 - k], ry = MIDDLE_Y * unit + bobbed[6 + k];
        ring(frame, (2 * k) * PIECES + 10, lx, ly, a, b, r, width);
        ring(frame, (2 * k + 1) * PIECES + 10, rx, ry, a, b, r, width);
        // the highlights slide against the ring's motion: its push along the chain, and its pulse
        // the ring's inside, to the middle of its outline
        const i32 w = 2 * a - unit, h = 2 * b - unit, full = clamp(shine, 0, unit);
        for (u32 side = 0; side < 2; ++side) {
            const u32 first = (2 * k + side) * PIECES;
            const i32 cx = side ? rx : lx, cy = side ? ry : ly;
            if (fill_mode == FILL_RISES) {
                // up from the foot, as wide as fits the ring's corners: a capsule as tall as the level, its round
                // ends tangent to the corners' (a disc in a round ring, the whole width in a square one)
                const i32 level = ((h >> 4) * full) >> 6;
                const i32 across = clamp(w - 2 * r + level, level < w ? level : w, w);
                capsule(frame, first, cx, cy + ((h - level) >> 1), across, level, full ? FILL_SHADE + 3 : 0);
            } else if (fill_mode == FILL_SWELLS) {
                // in proportion, the ring's own shape at every size
                capsule(frame, first, cx, cy, ((w >> 4) * full) >> 6, ((h >> 4) * full) >> 6, full ? FILL_SHADE + 3 : 0);
            } else {
                capsule(frame, first, cx, cy, w, h, steps ? FILL_SHADE + static_cast<u32>(steps - 1) : 0);
            }
        }
    }
}
} // namespace metal
