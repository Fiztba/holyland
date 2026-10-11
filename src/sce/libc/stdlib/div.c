/* libc.a member div.o (newlib libc/stdlib/div.c) (KFIV: not in ICO) */
#include <stdlib.h>

typedef struct {
    int quot;
    int rem;
} div_t;

div_t div(int numer, int denom)
{
    div_t r;

    r.quot = numer / denom;
    r.rem = numer % denom;
    if (numer >= 0 && r.rem < 0) {
        ++r.quot;
        r.rem -= denom;
    } else if (numer < 0 && r.rem > 0) {
        --r.quot;
        r.rem += denom;
    }
    return r;
}
