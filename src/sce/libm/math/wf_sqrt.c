/* libm.a member wf_sqrt.o (KFIV; not linked by ICO) */
#include <math.h>
#include <errno.h>
#include <math_private.h>

#define DOMAIN 1
#define EDOM 33

float sqrtf(float x)
{
    float z;
    struct exception exc;

    z = __ieee754_sqrtf(x);
    if (_LIB_VERSION == _IEEE_ || isnanf(x))
        return z;
    if (x < (float)0.0) {
        /* sqrtf(negative) */
        exc.type = DOMAIN;
        exc.name = "sqrtf";
        exc.err = 0;
        exc.arg1 = exc.arg2 = (double)x;
        if (_LIB_VERSION == _SVID_)
            exc.retval = 0.0;
        else
            exc.retval = 0.0 / 0.0;
        if (_LIB_VERSION == _POSIX_)
            *__errno() = EDOM;
        else if (!matherr(&exc)) {
            *__errno() = EDOM;
        }
        if (exc.err != 0)
            *__errno() = exc.err;
        return (float)exc.retval;
    } else
        return z;
}
