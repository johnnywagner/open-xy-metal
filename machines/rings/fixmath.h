// Fixed-point helpers for rings: Q31 multiply, shifts, and table-based sine, log2, exp2 and softplus. Every
// transcendental the voice needs is made of these, so the render path holds no float, no division and no 64-bit
// arithmetic. log2 values are Q24 (LOGQ).
#pragma once
#include <opxy/q31.hpp>
#include "tables.h"

namespace rings {

const i32 MAX = opxy::q31::maximum;
const i32 ONE_LOG = 1 << 24;      // 1.0 in a Q24 log2 value

inline i32 mul(i32 a, i32 b) { return opxy::q31::multiply(a, b); }
inline i32 half(i32 a) { return opxy::q31::half(a); }
// Floor of a / 2^n for any sign, defined in C++11.
inline i32 sar(i32 a, int n) { return a >= 0 ? a >> n : ~((~a) >> n); }
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline i32 absolute(i32 v) { return v < 0 ? -v : v; }
// Products with coefficients stored with fewer integer bits: Q30 (|c| < 2) and Q29 (|c| < 4).
inline i32 mul30(i32 a, i32 c) { return mul(a, c) * 2; }
inline i32 mul29(i32 a, i32 c) { return mul(a, c) * 4; }

// sin of a 32-bit phase (2^32 per cycle), Q30, interpolated from 2048 points.
inline i32 sine(u32 phase) {
    const u32 i = phase >> 21;
    const i32 f = static_cast<i32>((phase & 0x1fffffu) << 10);
    return SINE[i] + mul(SINE[i + 1] - SINE[i], f);
}
inline i32 cosine(u32 phase) { return sine(phase + 0x40000000u); }

// log2 of an unsigned integer, Q24; 0 gives -64.
inline i32 log2u(u32 x) {
    if (!x) return -64 * ONE_LOG;
    i32 e = 31;
    if (!(x & 0xffff0000u)) { x <<= 16; e -= 16; }
    if (!(x & 0xff000000u)) { x <<= 8; e -= 8; }
    if (!(x & 0xf0000000u)) { x <<= 4; e -= 4; }
    if (!(x & 0xc0000000u)) { x <<= 2; e -= 2; }
    if (!(x & 0x80000000u)) { x <<= 1; e -= 1; }
    const u32 f = x & 0x7fffffffu;
    const u32 i = f >> 21;
    const i32 m = LOG2T[i] + mul(LOG2T[i + 1] - LOG2T[i], static_cast<i32>((f & 0x1fffffu) << 10));
    return e * ONE_LOG + (m >> 6);
}
// log2 of a positive Q-format value with `bits` fraction bits.
inline i32 log2q(i32 x, int bits) { return log2u(static_cast<u32>(x)) - bits * ONE_LOG; }

// 2^(l / 2^24) as an unsigned integer, rounded and saturated.
inline u32 exp2u(i32 l) {
    const i32 n = sar(l, 24);
    if (n >= 32) return 0xffffffffu;
    const u32 f = static_cast<u32>(l) & 0xffffffu;
    const u32 i = f >> 14;
    const u32 m = EXP2T[i] + static_cast<u32>(mul(static_cast<i32>(EXP2T[i + 1] - EXP2T[i]),
                                                  static_cast<i32>((f & 0x3fffu) << 17)));   // Q30, 1..2
    if (n >= 30) {
        const int s = n - 30;
        return s && m >= 0x80000000u ? 0xffffffffu : m << s;
    }
    const int s = 30 - n;
    if (s >= 32) return 0;
    return (m + (1u << (s - 1))) >> s;
}
// 2^(l / 2^24) in Q`bits`, saturated to the signed maximum.
inline i32 exp2q(i32 l, int bits) {
    const u32 v = exp2u(l + bits * ONE_LOG);
    return v > 0x7fffffffu ? MAX : static_cast<i32>(v);
}

// log2(1 + 2^t), Q24 in and out: what log2(1 + X^2) is from 2 log2 X, and log2(a + b) from log2 a and log2 b.
inline i32 softplus2(i32 t) {
    const i32 a = absolute(t);
    const i32 base = t > 0 ? t : 0;
    if (a >= 32 * ONE_LOG) return base;
    const u32 i = static_cast<u32>(a) >> 19;
    const i32 f = static_cast<i32>((static_cast<u32>(a) & 0x7ffffu) << 12);
    return base + ((SP2T[i] + mul(SP2T[i + 1] - SP2T[i], f)) >> 6);
}

// sqrt of a Q31 value in [0, 1].
inline i32 sqrt31(i32 x) {
    if (x <= 0) return 0;
    if (x == MAX) return MAX;
    return exp2q(sar(log2q(x, 31), 1), 31);
}
}
