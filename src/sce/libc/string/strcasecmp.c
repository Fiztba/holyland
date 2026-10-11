/* libc.a member strcasecmp.o (newlib libc/string/strcasecmp.c). KFIV: not
   linked by ICO. */
#include <ctype.h>

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 != '\0' && tolower(*s1) == tolower(*s2)) {
        s1++;
        s2++;
    }

    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}
