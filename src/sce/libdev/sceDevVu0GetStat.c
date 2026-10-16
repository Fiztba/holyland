/* libdev.a: sceDevVu0GetStat (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0GetStat(void)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi29);
    if (s & 0x2) {
        return 1;
    }
    if (s & 0x4) {
        return 2;
    }
    if (s & 0x8) {
        return 3;
    }
    return (s & 0x1) ? 4 : 0;
}
