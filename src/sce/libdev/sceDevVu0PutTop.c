/* libdev.a: sceDevVu0PutTop (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0PutTop(unsigned short top)
{
    LIBDEV_CTC2(top, vi31);
}
