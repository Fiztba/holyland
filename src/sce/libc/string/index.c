/* libc.a member index.o (newlib libc/string/index.c) (KFIV: not in ICO) */
#include <string.h>

char *strchr(const char *s, int c);

char *index(const char *s, int c)
{
    return strchr(s, c);
}
