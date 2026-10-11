/* libm.a member s_scalbn.o (KFIV: not in ICO) */
#include <math.h>
#include <math_private.h>

/* fdlibm s_scalbn.c, double scalbn(double x, int n): x * 2**n. */

typedef union {
    double value;
    struct {
        int lsw;
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

double copysign(double x, double y); /* definition in sce/ */

static const double two54 = 1.80143985094819840000e+16;   /* 0x43500000, 0x00000000 */
static const double twom54 = 5.55111512312578270212e-17;  /* 0x3C900000, 0x00000000 */
static const double huge = 1.0e+300;
static const double tiny = 1.0e-300;

double scalbn(double x, int n)
{
    int k, hx, lx;
    EXTRACT_WORDS(hx, lx, x);
    k = (hx & 0x7ff00000) >> 20; /* extract exponent */
    if (k == 0) {                /* 0 or subnormal x */
        if ((lx | (hx & 0x7fffffff)) == 0)
            return x; /* +-0 */
        x *= two54;
        GET_HIGH_WORD(hx, x);
        k = ((hx & 0x7ff00000) >> 20) - 54;
        if (n < -50000)
            return tiny * x; /*underflow*/
    }
    if (k == 0x7ff)
        return x + x; /* NaN or Inf */
    k = k + n;
    if (k > 0x7fe)
        return huge * copysign(huge, x); /* overflow  */
    if (k > 0) {                         /* normal result */
        SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
        return x;
    }
    if (k <= -54) {
        if (n > 50000) /* in case integer overflow in n+k */
            return huge * copysign(huge, x); /*overflow*/
        else
            return tiny * copysign(tiny, x); /*underflow*/
    }
    k += 54; /* subnormal result */
    SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
    return x * twom54;
}
