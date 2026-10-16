/* libdev.a: sceDevVu0SetTBit (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0SetTBit(void)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    s |= 0x200;
    LIBDEV_CTC2(s, vi28);
}
