/* libdev.a: sceDevVu0ChkCnd (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0ChkCnd(void)
{
    unsigned int s;
    int ret = 0;

    switch (sceDevVu0GetCnd()) {
    case 0:
    case 3:
        ret = 1;
        break;
    case 1:
    case 2:
        break;
    case 4:
        LIBDEV_CFC2(s, vi28);
        s |= 0x100;
        LIBDEV_CTC2(s, vi28);
        ret = 1;
        break;
    default:
        break;
    }
    return ret;
}
