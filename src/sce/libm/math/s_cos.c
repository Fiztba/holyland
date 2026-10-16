/* libm.a member s_cos.o (KFIV: not in ICO) */
#include <math_private.h>
#include <math.h>

typedef union {
    double value;
    struct {
        unsigned int lsw;
        unsigned int msw;
    } parts;
} ieee_double_shape_type;

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


double __kernel_sin(double x, double y, int iy);
double __kernel_cos(double x, double y);
int __ieee754_rem_pio2(double x, double *y);

double cos(double x)
{
    double y[2], z = 0.0;
    int n, ix;

    GET_HIGH_WORD(ix, x);
    ix &= 0x7fffffff;

    if (ix <= 0x3fe921fb) {
        return __kernel_cos(x, z);
    } else if (ix >= 0x7ff00000) {
        return x - x;
    } else {
        n = __ieee754_rem_pio2(x, y);
        switch (n & 3) {
        case 0:
            return __kernel_cos(y[0], y[1]);
        case 1:
            return -__kernel_sin(y[0], y[1], 1);
        case 2:
            return -__kernel_cos(y[0], y[1]);
        default:
            return __kernel_sin(y[0], y[1], 1);
        }
    }
}
