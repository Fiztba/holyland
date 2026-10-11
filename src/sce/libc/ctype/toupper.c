/* libc.a member toupper.o (newlib libc/ctype/toupper.c) (KFIV: not in ICO) */
#include <ctype.h>

#undef toupper
int toupper(int c)
{
    return islower(c) ? c - 'a' + 'A' : c;
}
