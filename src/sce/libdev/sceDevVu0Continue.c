/* libdev.a: sceDevVu0Continue (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0Continue(void)
{
    int ret = 0;
    int st = sceDevVu0GetStat();

    if (st < 4) {
        if (st > 0) {
            ret = 1;
            sceDevVu0Start(sceDevVu0GetTpc());
        }
    }
    return ret;
}
