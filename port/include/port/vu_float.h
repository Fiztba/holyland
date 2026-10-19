/* VU float arithmetic, bit for bit as VU1 does it in port/platform/vu1.c, for the EE's VU0 macro-mode
   instructions (the Vu_* helpers of the game's math/vu0.h).

   On the PS2, VU0 and VU1 are the same floating-point units. The game relies on that: it draws some
   models twice, once with vertices the EE projects on VU0 (a PATH2 packet) and once with the same
   vertices projected by a VU1 program, and the second pass depth-tests GEQUAL against the first's Z.
   Unless both passes give the same screen positions to the 1/16 pixel, the second pass misses lines
   of pixels (Wobble_DrawRippledModel's water). So VU0 must round as VU1.

   The rules are vu1.c's: an exponent of 255 is an ordinary number (no infinities or NaNs, the
   largest value is 0x7FFFFFFF), denormals read as zero, results round toward zero, a result too large
   is +-0x7FFFFFFF and one too small +-0. Each operation is IEEE's rounded toward zero, computed here
   exactly without touching the rounding mode, so it gives the same bits in any mode and on any thread.
   The values are float bit patterns (uint32_t). */
#ifndef PORT_VU_FLOAT_H
#define PORT_VU_FLOAT_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#define VU_FLOAT_MAX 0x7FFFFFFFu
#define VU_FLOAT_SIGN 0x80000000u

/* A VU float as a double, exactly */
static inline double vu_float_value(uint32_t bits) {
    uint32_t e = (bits >> 23) & 0xFF;
    uint64_t d;
    double v;

    if (e == 0) {
        d = (uint64_t)(bits & VU_FLOAT_SIGN) << 32;
    } else {
        d = ((uint64_t)(bits & VU_FLOAT_SIGN) << 32) | ((uint64_t)(e + 896) << 52) |
            ((uint64_t)(bits & 0x007FFFFFu) << 29);
    }
    memcpy(&v, &d, 8);
    return v;
}

/* A double whose value needs no rounding beyond truncation (it is exact, or already rounded toward
   zero, or never within a double's rounding of a VU float boundary) as a VU float, rounded toward
   zero: the 24 bits kept, too small +-0, too large +-0x7FFFFFFF */
static inline uint32_t vu_float_round(double v) {
    uint64_t d;
    uint32_t sign;
    int e;

    memcpy(&d, &v, 8);
    sign = (uint32_t)(d >> 32) & VU_FLOAT_SIGN;
    e = (int)((d >> 52) & 0x7FF) - 896;
    if (v == 0.0 || e <= 0) {
        return sign;
    }
    if (e > 255) {
        return sign | VU_FLOAT_MAX;
    }
    return sign | ((uint32_t)e << 23) | (uint32_t)((d >> 29) & 0x007FFFFFu);
}

/* a + b. Two VU floats whose exponents are at most 28 apart add exactly in a double. Further apart,
   the smaller is less than half a unit of the larger's last place: rounded toward zero, the sum is the
   larger, or one unit less in magnitude when they have opposite signs. */
static inline uint32_t vu_float_add(uint32_t a, uint32_t b) {
    uint32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;

    if (ea == 0 || eb == 0 || (ea > eb ? ea - eb : eb - ea) <= 28) {
        return vu_float_round(vu_float_value(a) + vu_float_value(b));
    }
    if (ea < eb) {
        uint32_t t = a;

        a = b;
        b = t;
    }
    if (((a ^ b) & VU_FLOAT_SIGN) == 0) {
        return a;
    }
    return (a & 0x7FFFFFFFu) > 0x00800000u ? a - 1 : a & VU_FLOAT_SIGN; /* below the smallest: +-0 */
}

static inline uint32_t vu_float_sub(uint32_t a, uint32_t b) {
    return vu_float_add(a, b ^ VU_FLOAT_SIGN);
}

/* a * b: the product of two 24-bit significands is exact in a double */
static inline uint32_t vu_float_mul(uint32_t a, uint32_t b) {
    return vu_float_round(vu_float_value(a) * vu_float_value(b));
}

/* a / b; by zero, the largest float with the quotient's sign (0/0 too). A quotient of two 24-bit
   significands that is not on a VU float boundary is more than 2^-49 of itself away from one, so a
   double's rounding (in any mode) never carries it across. */
static inline uint32_t vu_float_div(uint32_t a, uint32_t b) {
    if ((b & 0x7F800000u) == 0) {
        return ((a ^ b) & VU_FLOAT_SIGN) | VU_FLOAT_MAX;
    }
    return vu_float_round(vu_float_value(a) / vu_float_value(b));
}

/* the square root of |a| (as the division, a double's rounding never crosses a boundary) */
static inline uint32_t vu_float_sqrt(uint32_t a) {
    return vu_float_round(sqrt(fabs(vu_float_value(a))));
}

/* MAX: sign and magnitude compared, so -0 < +0; an operand unchanged */
static inline uint32_t vu_float_max(uint32_t a, uint32_t b) {
    if ((int32_t)a < 0 && (int32_t)b < 0) {
        return (int32_t)a < (int32_t)b ? a : b;
    }
    return (int32_t)a > (int32_t)b ? a : b;
}

/* FTOIn: times 2^shift, truncated; at 2^31 and beyond the integer's limit with the float's sign */
static inline int32_t vu_float_ftoi(uint32_t a, int shift) {
    uint32_t e = (a >> 23) & 0xFF;

    if (e >= (uint32_t)(158 - shift)) {
        return (int32_t)((a >> 31) + 0x7FFFFFFFu);
    }
    return (int32_t)(vu_float_value(a) * (double)(1 << shift));
}

/* ITOFn: the integer over 2^shift, rounded toward zero (the double is exact) */
static inline uint32_t vu_float_itof(int32_t v, int shift) {
    return vu_float_round((double)v / (double)(1 << shift));
}

/* Float bits to and from the float slots the VU0 registers are kept in */
static inline uint32_t vu_float_bits(float f) {
    uint32_t bits;

    memcpy(&bits, &f, 4);
    return bits;
}

static inline float vu_float_from_bits(uint32_t bits) {
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

#endif
