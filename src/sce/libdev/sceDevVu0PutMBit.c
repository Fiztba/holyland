/* libdev.a: sceDevVu0PutMBit (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0PutMBit(int on)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    if (on == 1) {
        s |= 0x800;
    } else {
        s &= ~0x800;
    }
    LIBDEV_CTC2(s, vi28);
}
