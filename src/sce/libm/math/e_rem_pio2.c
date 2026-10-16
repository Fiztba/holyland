/* libm.a member e_rem_pio2.o (KFIV: not in ICO) */
#include <math.h>
#include <math_private.h>

/* fdlibm e_rem_pio2.c, int __ieee754_rem_pio2(double x, double *y): reduce x
   modulo pi/2 into y[0]+y[1], returning the quadrant n. */

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
#define GET_LOW_WORD(i, d)                                                                         \
    do {                                                                                           \
        ieee_double_shape_type gl_u;                                                               \
        gl_u.value = (d);                                                                          \
        (i) = gl_u.parts.lsw;                                                                      \
    } while (0)
#define SET_LOW_WORD(d, v)                                                                         \
    do {                                                                                           \
        ieee_double_shape_type sl_u;                                                               \
        sl_u.value = (d);                                                                          \
        sl_u.parts.lsw = (v);                                                                      \
        (d) = sl_u.value;                                                                          \
    } while (0)
#define SET_HIGH_WORD(d, v)                                                                        \
    do {                                                                                           \
        ieee_double_shape_type sh_u;                                                               \
        sh_u.value = (d);                                                                          \
        sh_u.parts.msw = (v);                                                                      \
        (d) = sh_u.value;                                                                          \
    } while (0)

double fabs(double x);
int __kernel_rem_pio2(double *x, double *y, int e0, int nx, int prec, const int *ipio2);

/* 2/pi in 24-bit chunks, for __kernel_rem_pio2 */
static const int two_over_pi[] = {
    0x00A2F983, 0x006E4E44, 0x001529FC, 0x002757D1, 0x00F534DD, 0x00C0DB62,
    0x0095993C, 0x00439041, 0x00FE5163, 0x00ABDEBB, 0x00C561B7, 0x00246E3A,
    0x00424DD2, 0x00E00649, 0x002EEA09, 0x00D1921C, 0x00FE1DEB, 0x001CB129,
    0x00A73EE8, 0x008235F5, 0x002EBB44, 0x0084E99C, 0x007026B4, 0x005F7E41,
    0x003991D6, 0x00398353, 0x0039F49C, 0x00845F8B, 0x00BDF928, 0x003B1FF8,
    0x0097FFDE, 0x0005980F, 0x00EF2F11, 0x008B5A0A, 0x006D1F6D, 0x00367ECF,
    0x0027CB09, 0x00B74F46, 0x003F669E, 0x005FEA2D, 0x007527BA, 0x00C7EBE5,
    0x00F17B3D, 0x000739F7, 0x008A5292, 0x00EA6BFB, 0x005FB11F, 0x008D5D08,
    0x00560330, 0x0046FC7B, 0x006BABF0, 0x00CFBC20, 0x009AF436, 0x001DA9E3,
    0x0091615E, 0x00E61B08, 0x00659985, 0x005F14A0, 0x0068408D, 0x00FFD880,
    0x004D7327, 0x00310606, 0x001556CA, 0x0073A8C9, 0x0060E27B, 0x00C08C6B,
};

/* pi/2 divided into 33+33+53-bit pieces, indexed by the high word of pi/2 multiples */
static const int npio2_hw[] = {
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
    0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
    0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
    0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
    0x404858EB, 0x404921FB,
};

static const double zero = 0.0, half = 5.00000000000000000000e-01,       /* 0x3FE00000, 0x00000000 */
    two24 = 1.67772160000000000000e+07,                                  /* 0x41700000, 0x00000000 */
    invpio2 = 6.36619772367581382433e-01,                                /* 0x3FE45F30, 0x6DC9C883 */
    pio2_1 = 1.57079632673412561417e+00,                                 /* 0x3FF921FB, 0x54400000 */
    pio2_1t = 6.07710050650619224932e-11,                                /* 0x3DD0B461, 0x1A626331 */
    pio2_2 = 6.07710050630396597660e-11,                                 /* 0x3DD0B461, 0x1A600000 */
    pio2_2t = 2.02226624879595063154e-21,                                /* 0x3BA3198A, 0x2E037073 */
    pio2_3 = 2.02226624871116645580e-21,                                 /* 0x3BA3198A, 0x2E000000 */
    pio2_3t = 8.47842766036889956997e-32;                                /* 0x397B839A, 0x252049C1 */

int __ieee754_rem_pio2(double x, double *y)
{
    double z, w, t, r, fn;
    double tx[3];
    unsigned int lz;
    int e0, i, j, nx, n, ix, hx;

    GET_HIGH_WORD(hx, x); /* high word of x */
    ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb) { /* |x| ~<= pi/4 , no need for reduction */
        y[0] = x;
        y[1] = 0;
        return 0;
    }
    if (ix < 0x4002d97c) { /* |x| < 3pi/4, special case with n=+-1 */
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3ff921fb) { /* 33+53 bit pi is good enough */
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            } else { /* near pi/2, use 33+33+53 bit pi */
                z -= pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        } else { /* negative x */
            z = x + pio2_1;
            if (ix != 0x3ff921fb) { /* 33+53 bit pi is good enough */
                y[0] = z + pio2_1t;
                y[1] = (z - y[0]) + pio2_1t;
            } else { /* near pi/2, use 33+33+53 bit pi */
                z += pio2_2;
                y[0] = z + pio2_2t;
                y[1] = (z - y[0]) + pio2_2t;
            }
            return -1;
        }
    }
    if (ix <= 0x413921fb) { /* |x| ~<= 2^19*(pi/2), medium size */
        t = fabs(x);
        n = (int)(t * invpio2 + half);
        fn = (double)n;
        r = t - fn * pio2_1;
        w = fn * pio2_1t; /* 1st round good to 85 bit */
        if (n < 32 && ix != npio2_hw[n - 1]) {
            y[0] = r - w; /* quick check no cancellation */
        } else {
            j = ix >> 20;
            y[0] = r - w;
            GET_HIGH_WORD(i, y[0]);
            i = j - (((unsigned int)i >> 20) & 0x7ff);
            if (i > 16) { /* 2nd iteration, good to 118 */
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = fn * pio2_2t - ((t - r) - w);
                y[0] = r - w;
                GET_HIGH_WORD(i, y[0]);
                i = j - (((unsigned int)i >> 20) & 0x7ff);
                if (i > 49) { /* 3rd iteration need 151 bits */
                    t = r;    /* will cover all possible cases */
                    w = fn * pio2_3;
                    r = t - w;
                    w = fn * pio2_3t - ((t - r) - w);
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        } else
            return n;
    }
    /*
     * all other (large) arguments
     */
    if (ix >= 0x7ff00000) { /* x is inf or NaN */
        y[0] = y[1] = x - x;
        return 0;
    }
    /* set z = scalbn(|x|,ilogb(x)-23) */
    GET_LOW_WORD(lz, x);
    SET_LOW_WORD(z, lz);
    e0 = (ix >> 20) - 1046; /* e0 = ilogb(z)-23; */
    SET_HIGH_WORD(z, ix - ((int)(e0 << 20)));
    for (i = 0; i < 2; i++) {
        tx[i] = (double)((int)(z));
        z = (z - tx[i]) * two24;
    }
    tx[2] = z;
    nx = 3;
    while (tx[nx - 1] == zero)
        nx--; /* skip zero term */
    n = __kernel_rem_pio2(tx, y, e0, nx, 2, two_over_pi);
    if (hx < 0) {
        y[0] = -y[0];
        y[1] = -y[1];
        return -n;
    }
    return n;
}
