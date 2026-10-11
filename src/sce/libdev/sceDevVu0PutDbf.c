/* libdev.a: sceDevVu0PutDbf (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0PutDbf(int on)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi28);
    if (on == 1) {
        s |= 0x8;
    } else {
        s &= ~0x8;
    }
    LIBDEV_CTC2(s, vi28);
}
