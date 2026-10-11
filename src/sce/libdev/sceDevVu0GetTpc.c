/* libdev.a: sceDevVu0GetTpc (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

unsigned int sceDevVu0GetTpc(void)
{
    unsigned int t;
    LIBDEV_CFC2(t, vi26);
    return t & 0xFFFF;
}
