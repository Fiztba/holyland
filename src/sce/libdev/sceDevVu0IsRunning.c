/* libdev.a: sceDevVu0IsRunning (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

int sceDevVu0IsRunning(void)
{
    unsigned int s;
    LIBDEV_CFC2(s, vi29);
    return (s & 0xF) == 1;
}
