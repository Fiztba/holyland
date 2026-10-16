/* libdev.a: sceDevVu0PutEBit (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0PutEBit(int on)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    if (on == 1) {
        s |= 0x400;
    } else {
        s &= ~0x400;
    }
    LIBDEV_CTC2(s, vi28);
}
