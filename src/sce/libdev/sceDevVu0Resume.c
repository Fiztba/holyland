/* libdev.a: sceDevVu0Resume (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0Resume(void)
{
    int ret = 0;
    int r = sceDevVu0GetCnd();

    if (r < 4) {
        if (r > 0) {
            ret = 1;
            sceDevVu0PutTop(sceDevVu0GetTop());
        }
    }
    return ret;
}
