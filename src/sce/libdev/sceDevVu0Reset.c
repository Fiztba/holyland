/* libdev.a: sceDevVu0Reset (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0Reset(void)
{
    unsigned int s;
    int ret = 0;

    switch (sceDevVu0GetStat()) {
    case 0:
    case 3:
        ret = 1;
        break;
    case 1:
    case 2:
        break;
    case 4:
        LIBDEV_CFC2(s, vi28);
        s |= 0x1;
        LIBDEV_CTC2(s, vi28);
        ret = 1;
        break;
    default:
        break;
    }
    return ret;
}
