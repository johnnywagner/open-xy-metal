// metal's page: the six modes as rings, mirrored about the middle like the two tines of a tuning fork. What drives
// what (the browser lab can reassign each; the OP-XY uses these):
// - STRETCH: how far apart they stand (the modes' ratios to the note, spread or squeezed).
// - RATIO: how wide they are, and which is tallest: the lowest mode at 0, moving out to the highest at 1.
// - MODES: which stand up: the modes it does not feed shrink to a third and are drawn thinner.
// - DROP: how squashed they are: wider and shorter as it turns up (each strike squashing them wider for a moment).
// A strike can also start the rings a set size larger or smaller (not here: STRIKE_SIZE is 1 x).
// On a strike the rings are a chain, like a slinky or a row of Newton's balls: a ring at one end is kicked (or the
// ends in turn, both ends, or the middle pair apart) and the push travels along ring by ring, each wiggling on its own
// spring as it passes, bouncing back off the ends; the coil widens each ring as its neighbours squeeze it. And each
// ring pulses, wider and narrower and out and in, at a rate that rises with its mode and dies away with its ring
// time. And each ring can bob up and down on its own, each at its own rate: on every note (dying away), or all the
// time. And the rings are squashed across DROP, held, with each strike's on top. Screen v37 (the lab's save C9).
// Where the OP-XY draws a ring's short straight pieces as boxes, node markers can take their place (MARKER).
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
//   bits 0-95: twenty settings (Setting below) of 5 bits or 4 (SETTING_BITS), each 0 for the value here, else 1 +
//     30ths (or 14ths) of the range;
//   bits 96-112: five assignments (Drive below), each a decimal digit: 0 for the knob here, 1 .. 4 a knob, 5 none (the
//     knob's default value; no squash), 6 .. 9 a knob inverted; three digits to 10 bits (bit 96), the last two to 7
//     (bit 106);
//   then the choices, each 0 for the one here: bit 113 the bob's other timing; bit 114 the squash's other direction;
//   bit 115 its other timing; bits 116-117 what a strike does to a held squash, counted on from the one here; bit 118
//   its other settling; bit 119 the chain's other ends; bits 120-121 where a strike kicks it, counted on likewise;
//   bits 122-123 the node markers' shape, counted on likewise.
enum Setting { KICK_SET, PASS_SET, WIGGLE_SET, RINGOUT_SET, PULSE_SET, STRIKE_SIZE_SET, BOB_DEPTH_SET, BOB_SPEED_SET,
               SPACE_DEPTH_SET, SQUASH_DEPTH_SET, SQUASH_KICK_SET, SQUASH_TIME_SET, SWING_DEPTH_SET, COIL_SET,
               HEIGHT_SET, SQUASH_HEIGHT_SET, PEAK_LOW_SET, PEAK_HIGH_SET, WIDTH_LOW_SET, WIDTH_HIGH_SET };
const u32 SETTING_BITS[20] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 5, 5, 5, 5, 5, 5, 4, 4};
enum Drive { SPACING, WIDTH, TALLEST_RING, STANDING, SQUASH };
const u32 KNOB_OF[5] = {3, 2, 2, 4, 1};   // each drive's knob (1 DROP, 2 RATIO, 3 STRETCH, 4 MODES, 5 none)
const i32 KNOB_REST[4] = {0, 91, 1023, 205};       // Q10: each knob's default, for a drive with no knob
const i32 KICK = 819;                  // Q10: the kick, 600 x pixels a second (480)
const i32 PASS = 443;                  // Q10: the coupling to each neighbour, 30 x 2^(6 x) per s^2 (181)
const i32 WIGGLE = 750;                // Q10: each ring's own spring, 4 x Hz (2.9 Hz)
const i32 RINGOUT = 68;                // Q10: the chain's decay, 0.25 x 24^x s to 1/e (0.31 s)
const i32 PULSE = 511;                 // Q10: the pulse, 0.7 x wider and narrower (35 %), and 6 x pixels out and in
const i32 STRIKE_SIZE = 511;           // Q10: a strike starts the rings 2^(2 x - 1) their size (1 x: no change)
const i32 BOB_DEPTH = 273;             // Q10: the bob, 48 x pixels either way (12.8)
const i32 BOB_SPEED = 443;             // Q10: its rate, 0.1 x 60^x Hz (0.59 Hz), each ring a little off it
const i32 SPACE_DEPTH = 511;           // Q10: how far the spacing moves with its knob, 2 x as far as STRETCH (1 x)
const bool BOB_ON_NOTES = true;        // on each note, dying away; else all the time
const i32 SWING_DEPTH = 307;           // Q10: how far the chain moves each ring on screen, 3 x its own motion (0.9 x)
const i32 COIL = 204;                  // Q10 (0.2): a ring pressed closer by its neighbours grows wider (pulled apart, narrower):
                                       // 1 x its width per 16 px of squeeze
const bool FREE_ENDS = false;          // the chain's ends: free (a push bounces back as a pull); else walls
enum KickFrom { ALTERNATE, LEFT_END, BOTH_ENDS, MIDDLE };   // a strike kicks one end, the ends in turn, both ends
const u32 KICK_FROM = BOTH_ENDS;                            // inwards, or the middle pair apart
const i32 HEIGHT = 750;                // Q10: every ring's height, 0.2 + x of its own (0.93 x)
const i32 WIDTH_LOW = 219;             // Q10: the rings' half width, 2 + 28 x pixels (8), with its knob at 0
const i32 WIDTH_HIGH = 585;            // and with its knob at full (18); between, in proportion
const i32 PEAK_LOW = 443;              // Q10: how far the tallest ring stands above the rest, 2 x (0.87 x), with its knob at 0
const i32 PEAK_HIGH = 477;             // and with its knob at full (0.93 x); between, in proportion
const i32 SQUASH_HEIGHT = 238;         // Q10: how much the squash changes the height, 1.5 x of the width's change (0.35 x)
const i32 SQUASH_DEPTH = 511;          // Q10: the squash, up to 0.8 x wider and shorter (40 %)
const bool SQUASH_BOTH_WAYS = false;   // its knob from tall and thin through none to short and wide; else none to wide
const bool SQUASH_HELD = true;         // held at its knob's amount (and a strike's on top); else only on each strike
const i32 SQUASH_KICK = 219;           // Q10: a strike's squash on a held one, 0.8 x (17 %; for RELEASE, 1 x)
enum SquashWay { FURTHER, WIDE, THIN, RELEASE };    // a strike pushes a held squash further its way, or short and wide,
const u32 SQUASH_WAY = WIDE;                        // or tall and thin, or lets it go back towards the rings' own shape
const i32 SQUASH_TIME = 219;           // Q10: a strike's squash settles over 0.1 x 16^x s (0.18 s)
const bool SQUASH_WOBBLE = false;      // it settles wobbling, overshooting 2.5 times as it dies away; else easing back
const i32 BOB_FADE_INV = 410;          // Q10 per second: a note's bob dies away over 2.5 s, squared
// Each ring's own bob rate, Q10 of the speed, so no two move together.
const i32 BOB_RATE[12] = {1024, 851, 1198, 932, 1116, 788, 1260, 973, 1075, 891, 1157, 819};
const i32 CHAIN_MOST = 40;             // pixels: no ring is pushed further
const i32 CHAIN_FASTEST = 1000;        // pixels a second: nor moves faster
const i32 PIECES = 10;                 // nodes per ring: the outline's 10 (gen_panel.py)
const i32 ROUNDING = 205;              // Q10: the corners, round at 0, square at 1
// The palette's first twelve: strokes 0.375 px x 1.405^i wide, Q10 (tools/gen_panel.py). A piece takes the widest
// within 1.133 of the width it should show, so it shows within 19 % of it: unscaled when THIN_LINES (the OP-XY draws
// a stroke at its palette width whatever the piece's scale, so every line shows thin, as intended), else divided by
// the piece's scale s (the browser's way, which the OP-XY draws as boxes where pieces are short: the node markers).
const bool THIN_LINES = true;
const i32 WIDTHS[12] = {384, 540, 758, 1065, 1496, 2102, 2954, 4150, 5831, 8193, 11511, 16172};
const i32 SMALLEST = 90;               // Q10: below this scale a piece would need a stroke wider than the palette's
// The node markers. The OP-XY draws a stroke at its palette entry's width whatever the piece's scale (and ignores line
// ends), so a short straight piece shows there as a box: its drawn length along the ring's edge and the palette's width
// across it (measured from photos and video of the device). ORIGINAL draws only the pieces, as the screen always has.
// ROUNDED rounds each box's corners: its ends become rounded squares (gen_panel.py's CORNER) as thick as the box, with
// the box's own stroke between them, shortened; a box thicker than long is two rounded squares as wide as it is long.
// Boxes thinner than ROUND_LEAST (the ring's own lines) or under half as long as thick (stubs) stay. SQUARE and DIAMOND
// put a filled rounded square (turned, for DIAMOND) the box's mean side across where a box is no more than MARKER_LONG
// times as long as it is thick, over a thin line; longer boxes stay.
enum Marker { ORIGINAL, ROUNDED, SQUARE, DIAMOND };
const u32 MARKER = ORIGINAL;           // the OP-XY's (only with THIN_LINES off); the browser lab can choose another
const i32 MARKER_LONG = 2560;          // Q10: 2.5 x
const i32 ROUND_LEAST = 3 * unit;      // Q10 pixels
const u32 MARKER_FILL = 12;            // the palette's marker fill
const u32 ROUNDS = 120;                // the first rounded square: eight a ring, two for each box, 16 px across

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

// The captions: each knob as the OP-XY shows its own, 0..99.
void captions(Frame& frame, const Input& input) {
    for (u32 k = 0; k < 4; ++k) {
        const i32 shown = ((clamp(input.parameters[k], 0, 0x7fffffff) >> 16) * 99 + 16384) >> 15;
        digits(frame.header[k], sizeof frame.header[k], shown, 0, false);
    }
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

// The palette entry for a piece at scale s that should show `width`: THIN_LINES, or making up for s.
inline u32 width_for(i32 s, i32 width) { return stroke_for(THIN_LINES ? unit : s, width); }

// A straight piece of `length` (Q10 pixels) centred at (x, y); vertical, or across when `across`. The vertical pieces
// are drawn 64 pixels long, the ones across 32. Returns its scale.
template <class F>
inline i32 straight(F& frame, u32 node, i32 x, i32 y, i32 length, bool across, i32 width) {
    const i32 s = clamp(across ? length >> 5 : length >> 6, SMALLEST, 2 * unit);
    place(frame, node, x, y, s, width_for(s, width), stroke);
    if (across) turn(frame, node, rotation(900));
    return s;
}

// A rounded square `side` across (Q10 pixels, its 1 px edge included) at (x, y), filled.
template <class F>
inline void rounded(F& frame, u32 node, i32 x, i32 y, i32 side) {
    place(frame, node, x, y, clamp((side - unit) >> 4, SMALLEST, 2 * unit), MARKER_FILL, fill_and_stroke);
}

// One of a ring's boxes as the OP-XY draws it: centred at (x, y), `length` along the ring's edge (across the screen
// when `across`, else down it) and palette entry `entry`'s width across it, Q10 pixels, from pieces `one` and `two`
// (the same piece for the top or bottom). Draws the marker, or its rounded ends, with rounded squares n and n + 1.
template <class F>
inline void box(F& frame, u32 n, u32 marker, i32 x, i32 y, bool across, i32 length, u32 entry, u32 one, u32 two,
                i32 width) {
    hide(frame, n);
    hide(frame, n + 1);
    const i32 thick = WIDTHS[entry];
    const i32 most = length > thick ? length : thick, least = length > thick ? thick : length;
    if (marker == ROUNDED) {
        if (thick < ROUND_LEAST || 2 * length < thick) return;
        // the two ends: rounded squares `end` across, `apart` either side of the middle along the long way
        const bool lengthwise = length >= thick;
        const i32 end = lengthwise ? thick : length, apart = (most - end) >> 1;
        const bool sideways = across == lengthwise;            // apart across the screen
        rounded(frame, n, sideways ? x - apart : x, sideways ? y : y - apart, end);
        rounded(frame, n + 1, sideways ? x + apart : x, sideways ? y : y + apart, end);
        // between them, the box's own stroke, shortened to reach from one end's middle to the other's
        if (two != one) hide(frame, two);
        if (!lengthwise || length < 2 * thick) {
            hide(frame, one);
            return;
        }
        const i32 core = length - thick;
        place(frame, one, x, y, clamp(across ? core >> 5 : core >> 6, SMALLEST, 2 * unit), entry, stroke);
        if (across) turn(frame, one, rotation(900));
        return;
    }
    if (marker == ORIGINAL || (most << 10) > MARKER_LONG * least) return;
    // The artwork is 16 px across; a diamond (the square turned) is 1.25 x the size from point to point, of 16 x 2^1/2.
    const i32 size = (length + thick) >> 1;
    place(frame, n, x, y, clamp(marker == DIAMOND ? (size * 57) >> 10 : size >> 4, SMALLEST, 2 * unit), MARKER_FILL,
          fill_and_stroke);
    if (marker == DIAMOND) turn(frame, n, rotation(450));
    // the pieces under it thin: in the stroke that shows `width` wide unscaled, as the OP-XY draws it
    shade(frame, one, stroke_for(unit, width), stroke);
    shade(frame, two, stroke_for(unit, width), stroke);
}

// Ring `index`: centre (cx, cy), half width a, half height b, corner radius r, all Q10 pixels. Its pieces in order:
// the quarter arcs top left, top right, bottom right and bottom left (drawn at radius 8, turned a quarter each), the
// two pieces down the left side, the two down the right, then the top and the bottom; then its node markers.
template <class F>
void ring(F& frame, u32 index, i32 cx, i32 cy, i32 a, i32 b, i32 r, i32 width, u32 marker) {
    const u32 first = index * PIECES;
    const i32 s = r >> 3;                         // the arcs' scale
    const i32 ax = a - r, by = b - r;             // the corners' centres, from the ring's
    if (s < SMALLEST) {
        for (u32 i = 0; i < 4; ++i) hide(frame, first + i);
    } else {
        const u32 shade = width_for(s, width);
        const i32 xs[4] = {cx - ax, cx + ax, cx + ax, cx - ax}, ys[4] = {cy - by, cy - by, cy + by, cy + by};
        for (u32 i = 0; i < 4; ++i) {
            place(frame, first + i, xs[i], ys[i], s, shade, stroke);
            if (i) turn(frame, first + i, rotation(900 * static_cast<i32>(i)));
        }
    }
    const i32 side = by;                          // half each side's straight length: two pieces cover it
    i32 down = 0;
    for (u32 j = 0; j < 2; ++j) {
        const i32 x = j ? cx + a : cx - a;
        down = straight(frame, first + 4 + 2 * j, x, cy - (side >> 1), side, false, width);
        straight(frame, first + 5 + 2 * j, x, cy + (side >> 1), side, false, width);
    }
    const i32 over = straight(frame, first + 8, cx, cy - b, 2 * ax, true, width);
    straight(frame, first + 9, cx, cy + b, 2 * ax, true, width);
    // The boxes the OP-XY draws: across the top and bottom, and each side's two pieces together.
    const u32 n = ROUNDS + index * 8, top = stroke_for(over, width), sides = stroke_for(down, width);
    box(frame, n, marker, cx, cy - b, true, 32 * over, top, first + 8, first + 8, width);
    box(frame, n + 2, marker, cx, cy + b, true, 32 * over, top, first + 9, first + 9, width);
    for (u32 j = 0; j < 2; ++j)
        box(frame, n + 4 + 2 * j, marker, j ? cx + a : cx - a, cy, false, side + 64 * down, sides, first + 4 + 2 * j,
            first + 5 + 2 * j, width);
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
    u32 at = 0;
    for (u32 i = 0; i < static_cast<u32>(setting); ++i) at += SETTING_BITS[i];
    const u32 wide = SETTING_BITS[setting], field = lab_bits(input, at, wide);
    if (!field) return fallback;
    return static_cast<i32>((field - 1) * (wide == 5 ? 34952 : 74898)) >> 10;   // 30ths or 14ths, as Q10
}
// The lab's digit for `drive` (0 .. 9).
inline u32 lab_digit(const Input& input, Drive drive) {
    const u32 group = static_cast<u32>(drive) / 3, place = static_cast<u32>(drive) % 3;
    u32 value = lab_bits(input, 96 + 10 * group, group ? 7 : 10);
    for (u32 i = 0; i < place; ++i) value /= 10;
    return value % 10;
}
// Its knob, 1 .. 4, or 5 for none: the lab's, or the one here.
inline u32 knob_for(const Input& input, Drive drive) {
    const u32 digit = lab_digit(input, drive);
    return digit == 0 ? KNOB_OF[drive] : digit <= 5 ? digit : digit - 5;
}
// What drives `drive`, Q10: its knob's value (inverted if the lab says), or that knob's default when it has none.
inline i32 driven(const Input& input, Drive drive) {
    const u32 knob = knob_for(input, drive);
    if (knob <= 4) {
        const i32 value = fraction(input.parameters[knob - 1]);
        return lab_digit(input, drive) >= 6 ? unit - 1 - value : value;
    }
    return KNOB_REST[KNOB_OF[drive] - 1];
}

// 2^e for e in [-2, 2] (Q10).
inline i32 exp2_wide(i32 e) {
    e = clamp(e, -2 * unit, 2 * unit);
    return e > unit ? exp2_q10(e - unit) << 1 : e < -unit ? exp2_q10(e + unit) >> 1 : exp2_q10(e);
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, SWING_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    const bool still = snapshot || input.reduced_motion;
    // the motions' settings: the lab's, or the ones above
    const bool on_notes = BOB_ON_NOTES != (lab_bits(input, 113, 1) != 0);
    const i32 bob_depth = 48 * lab_setting(input, BOB_DEPTH_SET, BOB_DEPTH);                         // Q10 px
    const i32 bob_speed = (102 * exp2_up((6049 * lab_setting(input, BOB_SPEED_SET, BOB_SPEED)) >> 10)) >> 10;    // Hz, Q10
    const i32 kick = 600 * lab_setting(input, KICK_SET, KICK);                                       // Q10 px/s
    const i32 pass = (30 * exp2_up(6 * lab_setting(input, PASS_SET, PASS))) >> 10;                     // per s^2
    const i32 wiggle = 4 * lab_setting(input, WIGGLE_SET, WIGGLE);                                     // Hz, Q10
    const i32 own = ((((wiggle * wiggle) >> 10) * 40426) >> 20);                                       // (2 pi f)^2
    const i32 fall = (4695 * lab_setting(input, RINGOUT_SET, RINGOUT)) >> 10;                          // x log2 24
    const i32 whole = fall > unit ? (fall + unit - 1) >> 10 : 1;
    const i32 damp = exp2_q10((whole << 10) - fall - unit) >> (whole - 1);            // 4 x 24^-x per s, Q8
    const i32 pulse = lab_setting(input, PULSE_SET, PULSE);                                          // Q10
    const i32 strike_size = 2 * lab_setting(input, STRIKE_SIZE_SET, STRIKE_SIZE) - unit;             // octaves, Q10
    const u32 marker = (MARKER + lab_bits(input, 122, 2)) & 3;
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
            const u32 from = (KICK_FROM + lab_bits(input, 120, 2)) & 3;
            const bool odd = ++state.strikes & 1;
            const i32 fastest = CHAIN_FASTEST * unit;
            if (from == MIDDLE) {
                state.speed[5] = clamp(state.speed[5] - kick, -fastest, fastest);
                state.speed[6] = clamp(state.speed[6] + kick, -fastest, fastest);
            } else {
                if (from != ALTERNATE || odd) state.speed[0] = clamp(state.speed[0] + kick, -fastest, fastest);
                if (from == BOTH_ENDS || (from == ALTERNATE && !odd))
                    state.speed[11] = clamp(state.speed[11] - kick, -fastest, fastest);
            }
        }
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // STRIKE_SIZE: the rings start 2^size their size (0.5 .. 2 x) and settle over 0.4 s in DROP's shape
    i32 grow = unit;
    if (state.drop_age < 410 && strike_size) {
        const i32 at = (state.drop_age * 2560) >> 10;                 // Q10 of the way through
        const i32 left = DISPLAY_DROP_LEFT[clamp((at * 23) >> 10, 0, 23)];
        grow = exp2_q10(clamp((strike_size * left) >> 10, -unit, unit));
    }

    // SQUASH: its knob, -1 (tall and thin) .. 1 (short and wide) both ways, else 0 .. 1, x 0.8 x depth; held, with
    // each strike's on top, or only from each strike. A strike's settles over its time: easing back in DROP's shape,
    // or wobbling, cos(2.5 turns) x (1 - t)^2.
    i32 squash = 0;
    if (knob_for(input, SQUASH) <= 4) {
        const bool both = SQUASH_BOTH_WAYS != (lab_bits(input, 114, 1) != 0);
        const bool held = SQUASH_HELD != (lab_bits(input, 115, 1) != 0);
        const u32 way = (SQUASH_WAY + lab_bits(input, 116, 2)) & 3;
        const bool wobble = SQUASH_WOBBLE != (lab_bits(input, 118, 1) != 0);
        const i32 knob = driven(input, SQUASH);
        const i32 depth = lab_setting(input, SQUASH_DEPTH_SET, SQUASH_DEPTH);
        const i32 amount = ((((both ? 2 * knob - unit : knob) * 819) >> 10) * depth) >> 10;
        // how much of a strike is left: 1 / its time is 10 x 2^-(4 x) per second
        const i32 e = 4 * lab_setting(input, SQUASH_TIME_SET, SQUASH_TIME);
        const i32 per_second = ((10 * unit * exp2_q10(-(e & (unit - 1)))) >> 10) >> (e >> 10);
        const i32 through = (state.drop_age * per_second) >> 10;            // Q10 of the way through
        i32 left = 0;
        if (through < unit) {
            if (wobble) {
                const i32 rest = unit - through;
                left = ((((rest * rest) >> 10)) * sine(((through * 9000) >> 10) + 900)) >> 10;
            } else {
                left = DISPLAY_DROP_LEFT[clamp((through * 23) >> 10, 0, 23)];
            }
        }
        if (!held) {
            squash = (amount * left) >> 10;
        } else {
            const i32 kick = lab_setting(input, SQUASH_KICK_SET, SQUASH_KICK);
            const i32 push = way == RELEASE ? -((amount * kick) >> 10)
                           : way == THIN ? -((kick * 819) >> 10)
                           : way == WIDE || amount >= 0 ? (kick * 819) >> 10 : -((kick * 819) >> 10);
            squash = clamp(amount + ((push * left) >> 10), -922, 922);      // never more than 0.9
        }
    }

    const bool pulses = !still && pulse > 0;
    const i32 step = clamp(elapsed, 0, 205);                            // a late frame moves at most 0.2 s on

    // The chain, stepped 1/128 s at a time: each ring is pulled back to its place by its own spring and towards its
    // neighbours by the coupling (past each end a wall, or nothing), and slowed by the ring-out.
    const bool free_ends = FREE_ENDS != (lab_bits(input, 119, 1) != 0);
    if (!still) {
        state.carry += step;
        const i32 steps = state.carry >> 3;
        state.carry &= 7;
        for (i32 n = 0; n < steps; ++n) {
            i32 push[12];
            for (u32 j = 0; j < 12; ++j) {
                const i32 end = free_ends ? state.at[j] : 0;                 // past an end: a wall, or nothing
                const i32 before = j ? state.at[j - 1] : end, after = j < 11 ? state.at[j + 1] : end;
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

    // SPACING, as STRETCH on the modes' log2 ratios: 2^((2 x - 1) depth), depth 0 .. 2
    const i32 spread = driven(input, SPACING);
    const i32 st = exp2_wide(((2 * spread - unit) * 2 * lab_setting(input, SPACE_DEPTH_SET, SPACE_DEPTH)) >> 10);
    const i32 width_low = 2 * unit + 28 * lab_setting(input, WIDTH_LOW_SET, WIDTH_LOW);
    const i32 width_high = 2 * unit + 28 * lab_setting(input, WIDTH_HIGH_SET, WIDTH_HIGH);
    const i32 a0 = width_low + (((width_high - width_low) * driven(input, WIDTH)) >> 10);   // half width, Q10 px
    const i32 peak = driven(input, TALLEST_RING) * 5;                   // the tallest mode, 0 .. 5, Q10
    const i32 standing = driven(input, STANDING) * 5;                   // Q10: mode k stands clamp(1 + 5x - k, 0, 1)
    const i32 low = (DISPLAY_L_RM[0] * st) >> 10;
    // the chain as drawn: each ring's push times the depth; and COIL, how much its neighbours squeeze it
    const i32 swing_depth = 3 * lab_setting(input, SWING_DEPTH_SET, SWING_DEPTH);
    const i32 coil = lab_setting(input, COIL_SET, COIL);
    const i32 height = 205 + lab_setting(input, HEIGHT_SET, HEIGHT);                 // Q10, 0.2 .. 1.2
    const i32 peak_low = 2 * lab_setting(input, PEAK_LOW_SET, PEAK_LOW), peak_high = 2 * lab_setting(input, PEAK_HIGH_SET, PEAK_HIGH);
    const i32 peak_height = peak_low + (((peak_high - peak_low) * driven(input, TALLEST_RING)) >> 10);   // Q10, 0 .. 2
    const i32 squash_height = (3 * lab_setting(input, SQUASH_HEIGHT_SET, SQUASH_HEIGHT)) >> 1;   // Q10, 0 .. 1.5
    i32 drawn[12], widen[12];
    for (u32 j = 0; j < 12; ++j) drawn[j] = (state.at[j] * swing_depth) >> 10;
    for (u32 j = 0; j < 12; ++j) {
        const i32 squeeze = (j < 11 ? drawn[j + 1] : drawn[j]) - (j ? drawn[j - 1] : drawn[j]);   // < 0: pressed
        widen[j] = clamp(unit - ((coil * (squeeze >> 4)) >> 10), 300, 2560);
    }
    for (u32 k = 0; k < 6; ++k) {
        const i32 fed = k == 0 ? unit : clamp(standing - static_cast<i32>(k - 1) * unit, 0, unit);
        const i32 width = 768 + ((666 * fed) >> 10);                    // stroke 0.75 .. 1.4 px
        const i32 octaves = ((DISPLAY_L_RM[k] * st) >> 10) - low;       // Q10, above the lowest mode
        const i32 out = 15 * unit + octaves * 36;                       // from the middle: 36 px an octave
        // The pulse: 2.5 Hz for the lowest mode, x 2^(0.35 octaves) above it, at most 9 Hz; it dies away as the
        // mode's ring on screen (DISPLAY_RING_INV) does, squared.
        const i32 hz = clamp((2560 * exp2_up((octaves * 358) >> 10)) >> 10, 0, 9 * unit);
        if (pulses) state.phases[k] += (static_cast<u32>(hz) * static_cast<u32>(step)) << 12;
        const i32 left = clamp(unit - ((state.ages[k] * DISPLAY_RING_INV[k]) >> 16), 0, unit);
        const i32 swing = pulses ? (((left * left) >> 10) * sine(static_cast<i32>(((state.phases[k] >> 20) * 3600) >> 12))) >> 10 : 0;
        const i32 sway = (swing * pulse) >> 10;                         // Q10, -1 .. 1 at the most
        // Its height: 1 - (d / 2.5)^2 of the way from 0.22 to 1 of TALL (times the peak's height), d its distance in
        // modes from the tallest;
        // then 0.3 .. 1 of that as it stands, and the strike's growth.
        const i32 d = ((static_cast<i32>(k) * unit - peak) * 410) >> 10;
        const i32 bump = clamp(unit - ((d * d) >> 10), 0, unit);
        const i32 shape = 225 + ((((799 * bump) >> 10) * peak_height) >> 10);
        const i32 size = (shape * (307 + ((717 * fed) >> 10))) >> 10;
        i32 a = a0 + (((a0 >> 4) * ((717 * sway) >> 10)) >> 6);        // up to 0.7 x wider and narrower
        // never shorter than it is wide: a ring shrinks to a circle, not a dash
        // (and room left above and below for the bob)
        i32 b = clamp((((TALL * size) >> 4) * grow) >> 6, a, TALLEST * unit - bob_depth);
        b = clamp(((b >> 4) * height) >> 6, 2 * unit, TALLEST * unit);   // HEIGHT
        // SQUASH: wider and shorter (or narrower and taller) by its amount, the height by its share
        a = clamp(a + (((a >> 4) * squash) >> 6), 2 * unit, 40 * unit);
        b = clamp(b - (((b >> 4) * ((squash * squash_height) >> 10)) >> 6), 2 * unit, TALLEST * unit);
        const i32 x = clamp(out + 6 * sway, 0, 260 * unit);             // the tines: out and in, up to 6 px
        // the chain runs left to right: the left rings from mode 5 in to mode 0, then the right from 0 out to 5
        for (u32 side = 0; side < 2; ++side) {
            const u32 j = side ? 6 + k : 5 - k;
            const i32 wide = clamp((a * widen[j]) >> 10, 2 * unit, 40 * unit);           // COIL
            // kept on screen, however tall, bobbed, wide and far pushed: 95 px above and below the centre line at
            // most, and 2 px in from the sides
            const i32 bob = bobbed[j] < 0 ? -bobbed[j] : bobbed[j];
            const i32 tall = clamp(b, 2 * unit, TALLEST * unit - bob);
            const i32 r = clamp((((wide < tall ? wide : tall) >> 4) * (unit - ROUNDING)) >> 6, 0, 16 * unit);
            const i32 cx = side ? MIDDLE_X * unit + x + drawn[j] : MIDDLE_X * unit - x + drawn[j];
            ring(frame, 2 * k + side, clamp(cx, wide + 2 * unit, 478 * unit - wide), MIDDLE_Y * unit + bobbed[j],
                 wide, tall, r, width, marker);
        }
    }
}
} // namespace metal
