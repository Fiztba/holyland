/* libdev.a: sceDevVu0Pause (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0Pause(void)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    s |= 0x2;
    LIBDEV_CTC2(s, vi28);
}
