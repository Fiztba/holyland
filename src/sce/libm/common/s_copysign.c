/* libm.a member s_copysign.o (KFIV: not in ICO) */
#include <math_private.h>
#include <math.h>

/* Double sign transfer on the high word, the fdlibm idiom (netlib's fdlibm,
   math_private.h): the member's own word access, no other member's helper. */

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

double copysign(double x, double y)
{
    unsigned int hx, hy;
    GET_HIGH_WORD(hx, x);
    GET_HIGH_WORD(hy, y);
    SET_HIGH_WORD(x, (hx & 0x7fffffff) | (hy & 0x80000000));
    return x;
}
