// metal's page: the six modes as rings, mirrored about the middle like the two tines of a tuning fork. What drives
// what (the browser lab can reassign each, or invert it; the OP-XY uses these):
// - STRETCH: how far apart they stand (the modes' ratios to the note, spread or squeezed).
// - RATIO: how wide they are, and which is tallest: the lowest mode at 0, moving out to the highest at 1.
// - MODES: their shape (round at 1, square at 6), and which stand up: the modes it does not feed shrink to a third
//   and are drawn thinner.
// - DROP: their size at a strike: larger or smaller as the pitch starts below or above the note, settling onto it.
// On a strike the rings are a chain, like a slinky or a row of Newton's balls: the ring at one end is kicked (the
// left and right ends in turn) and the push travels along ring by ring, each wiggling on its own spring as it
// passes, bouncing back off the far end. And each ring pulses, wider and narrower and out and in, at a rate that
// rises with its mode and dies away with its ring time.
// The lab also has, on no knob here: a figure for the rings (ring, triangle, diamond, figure 8, morphing between),
// hatched infill, their proportions, and two drifts, each ring on its own: a sway (tilting about its foot) and a bob
// (up and down). Screen work only.
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
const i32 PIECES = 14;                 // nodes per ring: 4 arcs, 4 long pieces, 2 short, 4 hatch lines

// The browser lab (never the OP-XY, which leaves parameter slots 4 to 7 at 0) tunes the motions and reassigns the
// knobs through those slots. A setting is a 9-bit field: 0 for the value here, else 1 + half a Q10 fraction. An
// assignment is a 4-bit field: bits 0-2 are 0 for the knob here, else knob 1 .. 4, or 5 for none (NONE_AT below);
// bit 3 inverts the knob.
//   slot 4: kick (bits 0-8), pass-along (9-17), wiggle (18-26); assignment 0 (27-30)
//   slot 5: ring-out, pulse, jolt; assignment 1
//   slot 6: drift (0-8); assignments 2 .. 6 (9-28)
//   slot 7: assignments 7 .. 12 (0-23)
enum Drive { SPACING, WIDTH, TALLEST_RING, SHAPE, STANDING, STRIKE_SIZE, CHAIN_AMOUNT, PULSE_AMOUNT,
             FIGURE, INFILL, PROPORTION, SWAY, BOB, DRIVES };
const u32 KNOB_OF[DRIVES] = {3, 2, 2, 4, 4, 1, 5, 5, 5, 5, 5, 5, 5};   // 1 DROP, 2 RATIO, 3 STRETCH, 4 MODES, 5 none
// Q10: each drive's value with no knob: its knob's default, the whole amount for the chain and the pulse, a ring,
// no infill, even proportions, no sway, no bob.
const i32 NONE_AT[DRIVES] = {1023, 91, 91, 205, 205, 0, 1024, 1024, 0, 0, 512, 0, 0};
const i32 KICK = 807;                  // Q10: the kick, 600 x pixels a second (473)
const i32 PASS = 436;                  // Q10: the coupling to each neighbour, 30 x 2^(6 x) per s^2 (176)
const i32 WIGGLE = 742;                // Q10: each ring's own spring, 4 x Hz (2.9 Hz)
const i32 RINGOUT = 59;                // Q10: the chain's decay, 0.25 x 24^x s to 1/e (0.3 s)
const i32 PULSE = 512;                 // Q10: the pulse, 0.7 x wider and narrower (35 %), and 6 x pixels out and in
const i32 JOLT = 512;                  // Q10: the strike's size change, 2 x as much as DROP's pitch (1 x)
const i32 DRIFT = 512;                 // Q10: the sway and bob's rate, 0.2 x 10^x Hz (0.63 Hz)
const i32 SWAY_MOST = 150;             // tenths of a degree either way, at the whole amount
const i32 BOB_MOST = 18;               // pixels either way, at the whole amount
// Each ring's own drift rate, Q10 of the drift, so no two move together.
const i32 DRIFT_RATE[12] = {1024, 851, 1198, 932, 1116, 788, 1260, 973, 1075, 891, 1157, 819};
const i32 CHAIN_MOST = 40;             // pixels: no ring is pushed further
const i32 CHAIN_FASTEST = 1000;        // pixels a second: nor moves faster
// The palette: white strokes 0.375 px x 1.284^i wide, Q10 (tools/gen_panel.py). A piece drawn at scale s takes the
// widest entry within 1.133 of the width it should show divided by s, so every piece shows within 12 % of it.
const i32 WIDTHS[16] = {384, 493, 633, 813, 1044, 1340, 1721, 2209, 2837, 3643, 4677, 6005, 7711, 9901, 12713, 16323};
const i32 SMALLEST = 90;               // Q10: below this scale a piece would need a stroke wider than the palette's
// atan(2^-i) in 1/160 degree, for cordic
const i32 ATAN[14] = {7200, 4250, 2246, 1140, 572, 286, 143, 72, 36, 18, 9, 4, 2, 1};

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

// A lab bit field of slot `slot`, `width` bits from `shift`.
inline u32 lab_bits(const Input& input, u32 slot, u32 shift, u32 width) {
    return (static_cast<u32>(input.slots[slot]) >> shift) & ((1u << width) - 1);
}
// A lab setting, Q10, or `fallback`.
inline i32 lab_setting(const Input& input, u32 slot, u32 shift, i32 fallback) {
    const u32 field = lab_bits(input, slot, shift, 9);
    return field ? static_cast<i32>(field - 1) * 2 : fallback;
}
// What drives `drive`, Q10: its knob's value (inverted if the lab says so), or NONE_AT with no knob.
inline i32 driven(const Input& input, Drive drive) {
    const u32 slot = drive == 0 ? 4 : drive == 1 ? 5 : drive <= 6 ? 6 : 7;
    const u32 shift = drive <= 1 ? 27 : drive <= 6 ? 9 + 4 * (drive - 2) : 4 * (drive - 7);
    const u32 field = lab_bits(input, slot, shift, 4);
    const u32 knob = (field & 7) >= 1 && (field & 7) <= 5 ? field & 7 : KNOB_OF[drive];
    if (knob > 4) return NONE_AT[drive];
    const i32 value = fraction(input.parameters[knob - 1]);
    return field & 8 ? unit - 1 - value : value;
}

// The palette entry for a piece at scale s (Q10) that should show a stroke `width` wide (Q10 pixels).
inline u32 stroke_for(i32 s, i32 width) {
    const i32 most = (width * 1160) >> 10;        // 1.133 x
    u32 entry = 0;
    for (u32 i = 1; i < 16; ++i)
        if (((WIDTHS[i] * s) >> 10) <= most) entry = i;
    return entry;
}

// The direction of (x, y) in tenths of a degree, counterclockwise from +x, by cordic; and its length (Q10, as the
// input). Shifts and adds only.
inline i32 direction(i32 x, i32 y, i32& length) {
    i32 angle = 0;
    if (x < 0) { x = -x; y = -y; angle = 1800 * 16; }
    for (i32 i = 0; i < 14; ++i) {
        const i32 nx = y > 0 ? x + (y >> i) : x - (y >> i);
        const i32 ny = y > 0 ? y - (x >> i) : y + (x >> i);
        angle += y > 0 ? ATAN[i] : -ATAN[i];
        x = nx; y = ny;
    }
    length = (x >> 4) * 622 >> 6;                 // x 0.6073, the cordic gain undone
    return angle >> 4;
}

// One piece of a figure, in the ring's own frame (Q10 pixels from its centre): where its centre is, how it is
// turned (tenths of a degree, clockwise; a straight piece at 0 runs up and down, an arc at 0 is the top-left
// quarter), and its length (an arc's radius).
struct Piece { i32 x, y, turn, length; };
struct Figure { Piece piece[10]; i32 hatch[4]; };   // hatch: the lines' lengths at 0.2 b up, 0.2 down, 0.6 up, 0.6 down

// A straight piece from (x0, y0) to (x1, y1).
inline Piece straight_piece(i32 x0, i32 y0, i32 x1, i32 y1) {
    Piece p;
    p.x = (x0 + x1) >> 1;
    p.y = (y0 + y1) >> 1;
    // a piece turned t runs along (-sin t, cos t): t is the direction of (y1 - y0, -(x1 - x0))
    p.turn = direction(y1 - y0, x0 - x1, p.length);
    return p;
}
inline Piece arc_piece(i32 x, i32 y, i32 turn, i32 radius) { Piece p = {x, y, turn, radius}; return p; }

// The four figures, each from the same ten pieces (4 arcs, 4 long pieces, 2 short), so they morph into each other.
// a, b: half width and height; r: the ring's corner radius; square: its squareness.
void ring_figure(Figure& f, i32 a, i32 b, i32 r, i32 square) {
    const i32 ax = a - r, by = b - r;
    f.piece[0] = arc_piece(-ax, -by, 0, r);
    f.piece[1] = arc_piece(ax, -by, 900, r);
    f.piece[2] = arc_piece(ax, by, 1800, r);
    f.piece[3] = arc_piece(-ax, by, 2700, r);
    f.piece[4] = arc_piece(-a, -(by >> 1), 0, by);
    f.piece[5] = arc_piece(-a, by >> 1, 0, by);
    f.piece[6] = arc_piece(a, -(by >> 1), 0, by);
    f.piece[7] = arc_piece(a, by >> 1, 0, by);
    f.piece[8] = arc_piece(0, -b, 900, 2 * ax);
    f.piece[9] = arc_piece(0, b, 900, 2 * ax);
    const i32 inner = (2 * a * square) >> 10;           // the straight-sided part
    const i32 near = inner + ((2 * a * ((unit - square) * 1004 >> 10)) >> 10);         // at 0.2: 0.98 of round
    const i32 far = inner + ((2 * a * ((unit - square) * 819 >> 10)) >> 10);           // at 0.6: 0.8 of round
    f.hatch[0] = f.hatch[1] = near;
    f.hatch[2] = f.hatch[3] = far;
}
void triangle_figure(Figure& f, i32 a, i32 b) {        // point up
    f.piece[0] = arc_piece(0, -b, 0, 0);
    f.piece[1] = arc_piece(0, -b, 900, 0);
    f.piece[2] = arc_piece(a, b, 1800, 0);
    f.piece[3] = arc_piece(-a, b, 2700, 0);
    f.piece[4] = straight_piece(-(a >> 1), 0, 0, -b);   // the left side, in two
    f.piece[5] = straight_piece(-a, b, -(a >> 1), 0);
    f.piece[6] = straight_piece(0, -b, a >> 1, 0);      // the right side, in two
    f.piece[7] = straight_piece(a >> 1, 0, a, b);
    f.piece[8] = arc_piece(-(a >> 1), b, 900, a);       // the base, in two
    f.piece[9] = arc_piece(a >> 1, b, 900, a);
    f.hatch[0] = (a * 819) >> 10;                       // 0.8 a at 0.2 up
    f.hatch[1] = (a * 1229) >> 10;                      // 1.2 a
    f.hatch[2] = (a * 410) >> 10;                       // 0.4 a
    f.hatch[3] = (a * 1638) >> 10;                      // 1.6 a
}
void diamond_figure(Figure& f, i32 a, i32 b) {
    f.piece[0] = arc_piece(0, -b, 0, 0);
    f.piece[1] = arc_piece(a, 0, 900, 0);
    f.piece[2] = arc_piece(0, b, 1800, 0);
    f.piece[3] = arc_piece(-a, 0, 2700, 0);
    f.piece[4] = straight_piece(-a, 0, 0, -b);
    f.piece[5] = straight_piece(0, b, -a, 0);
    f.piece[6] = straight_piece(0, -b, a, 0);
    f.piece[7] = straight_piece(a, 0, 0, b);
    // the short pieces lie along the lower sides, out of sight
    f.piece[8] = straight_piece(-(a >> 1) - (a >> 3), (b >> 1) - (b >> 3), -(a >> 1) + (a >> 3), (b >> 1) + (b >> 3));
    f.piece[9] = straight_piece((a >> 1) + (a >> 3), (b >> 1) - (b >> 3), (a >> 1) - (a >> 3), (b >> 1) + (b >> 3));
    f.hatch[0] = f.hatch[1] = (a * 1638) >> 10;
    f.hatch[2] = f.hatch[3] = (a * 819) >> 10;
}
void eight_figure(Figure& f, i32 a, i32 b) {           // a loop above, a crossing, a loop below
    const i32 p = a < 16 * unit ? a : 16 * unit;        // the loops' radius
    const i32 top = -b + p, bottom = b - p;
    f.piece[0] = arc_piece(0, top, 0, p);
    f.piece[1] = arc_piece(0, top, 900, p);
    f.piece[2] = arc_piece(0, bottom, 1800, p);
    f.piece[3] = arc_piece(0, bottom, 2700, p);
    f.piece[4] = straight_piece(-p, top, 0, 0);         // top left to bottom right, in two
    f.piece[5] = straight_piece(0, 0, p, bottom);
    f.piece[6] = straight_piece(p, top, 0, 0);          // top right to bottom left, in two
    f.piece[7] = straight_piece(0, 0, -p, bottom);
    f.piece[8] = straight_piece(-(p >> 1), top >> 1, -(p >> 2), top >> 2);   // out of sight on the crossing
    f.piece[9] = straight_piece(p >> 1, bottom >> 1, p >> 2, bottom >> 2);
    f.hatch[0] = f.hatch[1] = (p * 460) >> 10;
    f.hatch[2] = f.hatch[3] = (p * 1331) >> 10;
}

// Mixes figure g into f by t (Q10). Straight pieces turn the short way round, either end first.
void mix(Figure& f, const Figure& g, i32 t) {
    for (u32 i = 0; i < 10; ++i) {
        Piece& p = f.piece[i];
        const Piece& q = g.piece[i];
        i32 turn = q.turn - p.turn;
        const i32 half = i < 4 ? 1800 : 900;
        while (turn > half) turn -= 2 * half;
        while (turn <= -half) turn += 2 * half;
        p.x += ((q.x - p.x) >> 4) * t >> 6;
        p.y += ((q.y - p.y) >> 4) * t >> 6;
        p.turn += turn * t >> 10;
        p.length += ((q.length - p.length) >> 4) * t >> 6;
    }
    for (u32 i = 0; i < 4; ++i) f.hatch[i] += ((g.hatch[i] - f.hatch[i]) >> 4) * t >> 6;
}

void build(Figure& f, u32 which, i32 a, i32 b, i32 r, i32 square) {
    if (which == 0) ring_figure(f, a, b, r, square);
    else if (which == 1) triangle_figure(f, a, b);
    else if (which == 2) diamond_figure(f, a, b);
    else eight_figure(f, a, b);
}

// Draws a figure: its centre at (cx, cy), tilted by `tilt` (tenths of a degree, clockwise) about its foot (0, b),
// with `lines` of its four hatch lines showing (Q10, 0 .. 4).
template <class F>
void draw(F& frame, u32 first, const Figure& f, i32 cx, i32 cy, i32 b, i32 tilt, i32 lines, i32 width) {
    const Turn t = rotation(tilt);
    const auto at = [&](i32 x, i32 y, i32& px, i32& py) {
        const i32 dy = y - b;                       // from the foot
        px = cx + ((x >> 4) * t.cos >> 6) - ((dy >> 4) * t.sin >> 6);
        py = cy + b + ((x >> 4) * t.sin >> 6) + ((dy >> 4) * t.cos >> 6);
    };
    for (u32 i = 0; i < 10; ++i) {
        const Piece& p = f.piece[i];
        i32 x, y;
        at(p.x, p.y, x, y);
        const i32 s = i < 4 ? p.length >> 3 : i < 8 ? p.length >> 6 : p.length >> 5;   // drawn at 8, 64 and 32
        if (i < 4 && s < SMALLEST) { hide(frame, first + i); continue; }
        const i32 scale = clamp(s, SMALLEST, 2 * unit);
        place(frame, first + i, x, y, scale, stroke_for(scale, width), stroke);
        turn(frame, first + i, rotation(p.turn + tilt));
    }
    static const i32 HEIGHT[4] = {-205, 205, -614, 614};   // Q10 of b
    for (u32 i = 0; i < 4; ++i) {
        const i32 shown = clamp(lines - static_cast<i32>(i) * unit, 0, unit);
        const i32 length = ((f.hatch[i] >> 4) * 819 >> 6) * shown >> 10;    // 0.8 of the width there
        if (length < 2 * unit) { hide(frame, first + 10 + i); continue; }
        i32 x, y;
        at(0, (b >> 4) * HEIGHT[i] >> 6, x, y);
        const i32 scale = clamp(length >> 5, SMALLEST, 2 * unit);
        place(frame, first + 10 + i, x, y, scale, stroke_for(scale, width), stroke);
        turn(frame, first + 10 + i, rotation(900 + tilt));
    }
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.initialized = state.track = state.previous_event = 0;
    for (u32 k = 0; k < 6; ++k) { state.ages[k] = SWING_DONE; state.phases[k] = 0; }
    state.drop_age = SWING_DONE;
    for (u32 j = 0; j < 12; ++j) {
        state.at[j] = state.speed[j] = 0;
        state.sway[j] = static_cast<u32>(j) * 0x5A827999u;      // each ring starts its drifts somewhere else
        state.bob[j] = static_cast<u32>(j) * 0x6ED9EBA1u;
    }
    state.carry = 0;
    state.strikes = 0;
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    captions(frame, input);
    const i32 elapsed = clamp(input.elapsed, 0, SWING_DONE);
    const bool snapshot = !state.initialized || state.track != input.track;
    const bool still = snapshot || input.reduced_motion;
    // the motions' settings: the lab's, or the ones above
    const i32 kick = ((600 * lab_setting(input, 4, 0, KICK)) >> 10) * driven(input, CHAIN_AMOUNT);   // Q10 px/s
    const i32 pass = (30 * exp2_up(6 * lab_setting(input, 4, 9, PASS))) >> 10;                      // per s^2
    const i32 wiggle = 4 * lab_setting(input, 4, 18, WIGGLE);                                       // Hz, Q10
    const i32 own = ((((wiggle * wiggle) >> 10) * 40426) >> 20);                                    // (2 pi f)^2
    const i32 fall = (4695 * lab_setting(input, 5, 0, RINGOUT)) >> 10;                              // x log2 24
    const i32 whole = fall > unit ? (fall + unit - 1) >> 10 : 1;
    const i32 damp = exp2_q10((whole << 10) - fall - unit) >> (whole - 1);            // 4 x 24^-x per s, Q8
    const i32 pulse = (lab_setting(input, 5, 9, PULSE) * driven(input, PULSE_AMOUNT)) >> 10;         // Q10
    const i32 jolt = 2 * lab_setting(input, 5, 18, JOLT);                                           // Q10
    const i32 drift = (205 * exp2_up((3402 * lab_setting(input, 6, 0, DRIFT)) >> 10)) >> 10;        // Hz, Q10
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

    // The drifts: each ring's own sway and bob, always running, as much as their drives say.
    const i32 sway = driven(input, SWAY), bob = driven(input, BOB);
    if (!still)
        for (u32 j = 0; j < 12; ++j) {
            const u32 rate = static_cast<u32>((drift * DRIFT_RATE[j]) >> 10) * static_cast<u32>(step) << 12;
            state.sway[j] += rate;
            state.bob[j] += rate + (rate >> 3);
        }
    const i32 bob_px = (BOB_MOST * bob);                                // Q10 pixels

    const i32 st = exp2_q10(driven(input, SPACING) * 2 - unit);         // as STRETCH on the modes' log2 ratios
    const i32 lean = driven(input, PROPORTION) * 2 - unit;
    const i32 proportion = exp2_q10(lean);                              // taller x 0.5 .. 2, and thinner as much
    const i32 a0 = ((8 * unit + driven(input, WIDTH) * 10) * exp2_q10(-lean)) >> 10;   // half width 8 .. 18 px
    const i32 peak = driven(input, TALLEST_RING) * 5;                   // the tallest mode, 0 .. 5, Q10
    const i32 square = driven(input, SHAPE);                            // round at 0, square at 1
    const i32 standing = driven(input, STANDING) * 5;                   // Q10: mode k stands clamp(1 + 5x - k, 0, 1)
    const i32 shapes = driven(input, FIGURE) * 3;                       // Q10: ring, triangle, diamond, 8
    const u32 from = static_cast<u32>(clamp(shapes >> 10, 0, 2));
    // each morph takes the middle 40 % of the knob between two figures, so each figure holds around its own point
    const i32 towards = clamp(((shapes - static_cast<i32>(from) * unit - 307) * 2560) >> 10, 0, unit);
    const i32 lines = driven(input, INFILL) * 4;                        // Q10: hatch lines showing, 0 .. 4
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
        const i32 wobble = (swing * pulse) >> 10;                       // Q10, -1 .. 1 at the most
        // Its height: 1 - (d / 2.5)^2 of the way from 0.22 to 1 of TALL, d its distance in modes from the tallest;
        // then 0.3 .. 1 of that as it stands, the proportions and the strike's growth.
        const i32 d = ((static_cast<i32>(k) * unit - peak) * 410) >> 10;
        const i32 bump = clamp(unit - ((d * d) >> 10), 0, unit);
        const i32 shape = 225 + ((799 * bump) >> 10);
        const i32 size = (((shape * (307 + ((717 * fed) >> 10))) >> 10) * proportion) >> 10;
        const i32 a = a0 + (((a0 >> 4) * ((717 * wobble) >> 10)) >> 6);  // up to 0.7 x wider and narrower
        // never shorter than it is wide: a ring shrinks to a circle (a square at MODES 6), not a dash; and room
        // left above and below for the bob
        const i32 b = clamp((((TALL * size) >> 4) * grow) >> 6, a, TALLEST * unit - bob_px);
        const i32 r = clamp((((a < b ? a : b) >> 4) * (unit - square)) >> 6, 0, 16 * unit);
        Figure f, g;
        build(f, from, a, b, r, square);
        if (towards > 0) { build(g, from + 1, a, b, r, square); mix(f, g, towards); }
        const i32 x = out + 6 * wobble;                                 // the tines: out and in, up to 6 px
        for (u32 side = 0; side < 2; ++side) {
            const u32 j = side ? 6 + k : 5 - k;                         // left to right along the chain
            const i32 cx = MIDDLE_X * unit + (side ? x : -x) + state.at[j];
            const i32 cy = MIDDLE_Y * unit + ((bob_px >> 4) * sine(static_cast<i32>(((state.bob[j] >> 20) * 3600) >> 12)) >> 6);
            const i32 tilt = (SWAY_MOST * sway >> 10) * sine(static_cast<i32>(((state.sway[j] >> 20) * 3600) >> 12)) >> 10;
            draw(frame, (2 * k + side) * PIECES, f, cx, cy, b, tilt, lines, width);
        }
    }
}
} // namespace metal
