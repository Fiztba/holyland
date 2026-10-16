/* libm.a member e_atan2.o (KFIV: not in ICO) */
#include <math.h>
#include <math_private.h>

/* fdlibm e_atan2.c, double __ieee754_atan2(double y, double x): atan(y/x) in the right quadrant. */

typedef union {
    double value;
    struct {
        unsigned int lsw;
        int msw;
    } parts;
} ieee_double_shape_type;

#define EXTRACT_WORDS(hi, lo, d)                                                                   \
    do {                                                                                           \
        ieee_double_shape_type ew_u;                                                               \
        ew_u.value = (d);                                                                          \
        (hi) = ew_u.parts.msw;                                                                     \
        (lo) = ew_u.parts.lsw;                                                                     \
    } while (0)
#define GET_HIGH_WORD(i, d)                                                                        \
    do {                                                                                           \
        ieee_double_shape_type gh_u;                                                               \
        gh_u.value = (d);                                                                          \
        (i) = gh_u.parts.msw;                                                                      \
    } while (0)
#define SET_HIGH_WORD(d, v)                                                                        \
    do {                                                                                           \
        ieee_double_shape_type sh_u;                                                               \
        sh_u.value = (d);                                                                          \
        sh_u.parts.msw = (v);                                                                      \
        (d) = sh_u.value;                                                                          \
    } while (0)

double atan(double x);
double fabs(double x);

static const double tiny = 1.0e-300, zero = 0.0, pi_o_4 = 7.8539816339744827900E-01,
                    pi_o_2 = 1.5707963267948965580E+00, pi = 3.1415926535897931160E+00,
                    pi_lo = 1.2246467991473531772E-16;

double __ieee754_atan2(double y, double x)
{
    double z;
    int k, m, hx, hy, ix, iy;
    unsigned int lx, ly;

    EXTRACT_WORDS(hx, lx, x);
    ix = hx & 0x7fffffff;
    EXTRACT_WORDS(hy, ly, y);
    iy = hy & 0x7fffffff;
    if (((ix | ((lx | -lx) >> 31)) > 0x7ff00000) || ((iy | ((ly | -ly) >> 31)) > 0x7ff00000))
        return x + y; /* x or y is NaN */
    if (((hx - 0x3ff00000) | lx) == 0)
        return atan(y); /* x=1.0 */
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2); /* 2*sign(x)+sign(y) */

    /* when y = 0 */
    if ((iy | ly) == 0) {
        switch (m) {
        case 0:
        case 1:
            return y; /* atan(+-0,+anything)=+-0 */
        case 2:
            return pi + tiny; /* atan(+0,-anything) = pi */
        case 3:
            return -pi - tiny; /* atan(-0,-anything) =-pi */
        }
    }
    /* when x = 0 */
    if ((ix | lx) == 0)
        return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    /* when x is INF */
    if (ix == 0x7ff00000) {
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0:
                return pi_o_4 + tiny; /* atan(+INF,+INF) */
            case 1:
                return -pi_o_4 - tiny; /* atan(-INF,+INF) */
            case 2:
                return 3.0 * pi_o_4 + tiny; /*atan(+INF,-INF)*/
            case 3:
                return -3.0 * pi_o_4 - tiny; /*atan(-INF,-INF)*/
            }
        } else {
            switch (m) {
            case 0:
                return zero; /* atan(+...,+INF) */
            case 1:
                return -zero; /* atan(-...,+INF) */
            case 2:
                return pi + tiny; /* atan(+...,-INF) */
            case 3:
                return -pi - tiny; /* atan(-...,-INF) */
            }
        }
    }
    /* when y is INF */
    if (iy == 0x7ff00000)
        return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    /* compute y/x */
    k = (iy - ix) >> 20;
    if (k > 60)
        z = pi_o_2 + 0.5 * pi_lo; /* |y/x| >  2**60 */
    else if (hx < 0 && k < -60)
        z = 0.0; /* |y|/x < -2**60 */
    else
        z = atan(fabs(y / x)); /* safe to do y/x */
    switch (m) {
    case 0:
        return z; /* atan(+,+) */
    case 1:
    {
            int hz;
            GET_HIGH_WORD(hz, z);
            SET_HIGH_WORD(z, hz ^ 0x80000000);
            return z; /* atan(-,+) */
        }
    case 2:
        return pi - (z - pi_lo); /* atan(+,-) */
    default: /* case 3 */
        return (z - pi_lo) - pi; /* atan(-,-) */
    }
}
