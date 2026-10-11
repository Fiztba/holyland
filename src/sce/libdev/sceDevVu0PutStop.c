/* libdev.a: sceDevVu0PutStop (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0PutStop(int on)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    if (on == 1) {
        s |= 0x4;
    } else {
        s &= ~0x4;
    }
    LIBDEV_CTC2(s, vi28);
}
