/* libm.a member w_atan2.o (KFIV: not in ICO) */
#include <errno.h>

/* The declarations this member uses, as the libm entry points define them:
   math.h's isnan takes a long long, but this member hands it the double's
   bits in the integer argument register. */
struct exception {
    int type;
    char *name;
    double arg1;
    double arg2;
    double retval;
    int err;
};
extern const int __fdlib_version;
int matherr(struct exception *x);
int isnan(double d);
double __ieee754_atan2(double y, double x);

#define _IEEE_ (-1)
#define _POSIX_ 2
#define EDOM 33

double atan2(double y, double x)
{
    double z;
    struct exception exc;

    z = __ieee754_atan2(y, x);
    if (__fdlib_version == _IEEE_ || isnan(x) || isnan(y))
        return z;
    if (x == 0.0 && y == 0.0) {
        exc.arg1 = y;
        exc.arg2 = x;
        exc.type = 1;
        exc.name = "atan2";
        exc.err = 0;
        exc.retval = 0.0;
        if (__fdlib_version == _POSIX_)
            *__errno() = EDOM;
        else if (!matherr(&exc))
            *__errno() = EDOM;
        if (exc.err != 0)
            *__errno() = exc.err;
        return exc.retval;
    } else
        return z;
}
