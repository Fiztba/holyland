/* libdev.a: sceDevVu0GetDbf (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0GetDbf(void)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    return (s >> 3) & 1;
}
