/* The PS2's math library for the game (port/include/port/libm_ps2.h): newlib's, as the SDK built it
   into SLUS_203.18, so the results are the PS2's bit for bit and the same on every host.

   The functions themselves are the decomp's own sources (decomp/src/sce/libm), compiled with their
   names made ps2_* (port/sdk/libm_ps2_names.h, CMakeLists.txt). On the EE:
   - the float functions (sinf, cosf, atan2f, atanf, asinf, sqrtf) are float C on the FPU, which
     rounds toward zero without denormals. The game's threads run in that mode (MXCSR, port/
     platform/fiber.c), so compiled for the PC they compute as on the EE;
   - the double functions (sin, cos, sqrt, atan2, atan, acos) are double C in software, libgcc's
     dp-bit: IEEE doubles rounding to nearest, with denormals. They run here in the host's IEEE mode
     (ps2_soft_* inside a switch of MXCSR).
   tools/ee_libm_ref.py runs the retail functions on a model of the EE; tests/libm_test.c checks these
   against its results.

   Compiled for the PC, the float functions round as on the EE as long as the compiler folds the
   constant expressions ee-gcc folded (in nearest, at compile time: the static const scalars, such as
   atan2f's pi_o_2 + 0.5f * pi_lo) and no others. clang folds those too, and one more: atanf's
   atanhi[3] + atanlo[3], array elements that ee-gcc left to the FPU. So atanf is wrapped below.

   What is the port's own: sqrtf and its __ieee754_sqrtf, atanf's wrapper, isnan, errno. */
#include <stdint.h>
#include <string.h>

#include "port/libm_ps2.h"
#include "port/platform.h"

/* ---- newlib's support -------------------------------------------------------------------------- */
int ps2_errno;

int *ps2___errno(void) {
    return &ps2_errno;
}

/* libc's isnan takes the double's bits in an integer register (math.h says long long), and the
   wrappers that call it declare it as taking a double: on the EE those are the same register, on the
   PC they are not. This is its sum on the bits. */
int ps2_isnan(double x) {
    uint64_t bits;
    uint32_t lx, hx;

    memcpy(&bits, &x, 8);
    lx = (uint32_t)bits;
    hx = (uint32_t)(bits >> 32) & 0x7FFFFFFFu;
    hx |= (lx | (0u - lx)) >> 31;
    hx = 0x7FF00000u - hx;
    return (int)(hx >> 31);
}

/* ---- sqrtf's core -------------------------------------------------------------------------------
   newlib's ef_sqrt.c works out the root bit by bit in integers, then picks the last bit's rounding
   with `one - tiny` and `one + tiny` (1.0f and 1e-30f). ee-gcc folded both to 1.0 when it compiled
   the SDK (the retail code has no float operation there: it rounds to nearest, `q += q & 1`), so it is
   written folded here too, whatever the compiler and the rounding mode. Its other two paths are EE
   float operations: x * x + x for an exponent of 255 (a number on the EE) and (x - x) / (x - x) for a
   negative x, done in VU arithmetic, which is the EE FPU's (port/vu_float.h). */
#include "port/vu_float.h"

float ps2___ieee754_sqrtf(float x) {
    int32_t ix, s, q, m, t, i;
    uint32_t r;
    float z;

    memcpy(&ix, &x, 4);
    if ((ix & 0x7F800000) == 0x7F800000) {
        uint32_t b = (uint32_t)ix;

        return vu_float_from_bits(vu_float_add(vu_float_mul(b, b), b));
    }
    m = ix >> 23;
    if (ix <= 0) {
        if ((ix & 0x7FFFFFFF) == 0) {
            return x;
        }
        if (ix < 0) {
            uint32_t zero = vu_float_sub((uint32_t)ix, (uint32_t)ix);

            return vu_float_from_bits(vu_float_div(zero, zero));
        }
    }
    if (m == 0) { /* a denormal: the integer code takes it as it is */
        for (i = 0; (ix & 0x800000) == 0; i++) {
            ix <<= 1;
        }
        m -= i - 1;
    }
    m -= 0x7F;
    ix = (ix & 0x7FFFFF) | 0x800000;
    ix <<= (m & 1);
    m >>= 1;
    ix <<= 1;
    q = s = 0;
    r = 0x1000000;
    do {
        t = s + (int32_t)r;
        if (t <= ix) {
            s = t + (int32_t)r;
            ix -= t;
            q += (int32_t)r;
        }
        r >>= 1;
        ix <<= 1;
    } while (r != 0);
    if (ix != 0) {
        q += (q & 1);
    }
    ix = (q >> 1) + 0x3F000000;
    ix += (int32_t)((uint32_t)m << 23);
    memcpy(&z, &ix, 4);
    return z;
}

/* atanf: newlib's for |x| >= 2^34 adds atanhi[3] and atanlo[3] on the FPU at run time (rounding
   toward zero: 0x3FC90FDA), which clang would fold (0x3FC90FDB). volatile keeps them apart. */
float ps2_atanf_newlib(float x);

static volatile float atanhi3 = 1.570796251296997f, atanlo3 = 7.549789415861596e-08f;

float ps2_atanf(float x) {
    int32_t hx;
    uint32_t ix;

    memcpy(&hx, &x, 4);
    ix = (uint32_t)hx & 0x7FFFFFFFu;
    if (ix >= 0x50800000u) {
        if (ix > 0x7F800000u) { /* x + x, of a number with exponent 255 on the EE */
            return vu_float_from_bits(vu_float_add((uint32_t)hx, (uint32_t)hx));
        }
        if (hx > 0) {
            return atanhi3 + atanlo3;
        }
        return -atanhi3 - atanlo3;
    }
    return ps2_atanf_newlib(x);
}

/* sqrtf, newlib's wf_sqrt.c with the SDK's _LIB_VERSION (X/Open): a negative x gives the double
   0.0 / 0.0 turned into a float, which on the EE are dp-bit's NaN (0x7FF8000000000000) and its
   conversion (0x7FB00000), and errno EDOM (matherr returns 0). */
int ps2_isnanf(float x);

float ps2_sqrtf(float x) {
    float z = ps2___ieee754_sqrtf(x);

    if (ps2_isnanf(x)) {
        return z;
    }
    if (x < 0.0f) {
        uint64_t nan_bits = 0x7FF8000000000000ull;
        double nan;

        memcpy(&nan, &nan_bits, 8);
        ps2_errno = 33; /* EDOM */
        return ps2_dtof(nan);
    }
    return z;
}

/* ---- The double functions, in IEEE's mode -------------------------------------------------------
   dp-bit rounds to nearest and keeps denormals, which is SSE's default; the game's threads round
   toward zero without them, so each call switches. (The ps2_soft_* are compiled apart, so no double
   operation of theirs moves across the switch.) */
double ps2_soft_sin(double x);
double ps2_soft_cos(double x);
double ps2_soft_sqrt(double x);
double ps2_soft_atan2(double y, double x);
double ps2_soft_atan(double x);
double ps2_soft_acos(double x);

#define SOFT_DOUBLE(call)                                                                          \
    do {                                                                                           \
        uint32_t mode = port_host_float_begin();                                                   \
        double result = call;                                                                      \
        port_host_float_end(mode);                                                                 \
        return result;                                                                             \
    } while (0)

double ps2_sin(double x) {
    SOFT_DOUBLE(ps2_soft_sin(x));
}

double ps2_cos(double x) {
    SOFT_DOUBLE(ps2_soft_cos(x));
}

double ps2_sqrt(double x) {
    SOFT_DOUBLE(ps2_soft_sqrt(x));
}

double ps2_atan2(double y, double x) {
    SOFT_DOUBLE(ps2_soft_atan2(y, x));
}

double ps2_atan(double x) {
    SOFT_DOUBLE(ps2_soft_atan(x));
}

double ps2_acos(double x) {
    SOFT_DOUBLE(ps2_soft_acos(x));
}

/* ---- dp-bit's double -> float (dptofp) ----------------------------------------------------------
   What the EE's software doubles give where the game turns one into a float: dp-bit unpacks the
   double (the significand at bit 60), keeps its top 31 bits with a sticky bit for the rest, and
   fp-bit packs that: rounded to nearest even in the normal range; truncated, not rounded, in the
   denormal range; an infinity above it; a NaN with its top bits and fp-bit's quiet bit (0x100000).
   Done in integers, so it is the same in any rounding mode. */
float ps2_dtof(double x) {
    uint64_t d, mant, frac;
    uint32_t t, f;
    int e, exp;
    float out;

    memcpy(&d, &x, 8);
    e = (int)((d >> 52) & 0x7FF);
    mant = d & 0xFFFFFFFFFFFFFull;
    if (e == 0x7FF) {
        if (mant == 0) {
            f = 0x7F800000u;
        } else {
            t = (uint32_t)(mant >> 30) | ((mant & 0x3FFFFFFFu) != 0);
            f = 0x7F800000u | ((t | 0x100000u) & 0x7FFFFFu);
        }
    } else if (e == 0 && mant == 0) {
        f = 0;
    } else {
        if (e == 0) {
            exp = -1022;
            frac = mant << 8;
            while (frac < (1ull << 60)) {
                frac <<= 1;
                exp--;
            }
        } else {
            exp = e - 1023;
            frac = (mant << 8) | (1ull << 60);
        }
        t = (uint32_t)(frac >> 30) | ((frac & 0x3FFFFFFFu) != 0);
        if (exp < -126) {
            int shift = -126 - exp;

            f = shift > 25 ? 0 : (t >> shift) >> 7;
        } else if (exp > 127) {
            f = 0x7F800000u;
        } else {
            exp += 127;
            if ((t & 0x7F) == 0x40) {
                if (t & 0x80) {
                    t += 0x40;
                }
            } else {
                t += 0x3F;
            }
            if (t >= 0x80000000u) {
                t >>= 1;
                exp++;
            }
            f = ((uint32_t)exp << 23) | ((t >> 7) & 0x7FFFFFu);
        }
    }
    f |= (uint32_t)(d >> 32) & 0x80000000u;
    memcpy(&out, &f, 4);
    return out;
}
