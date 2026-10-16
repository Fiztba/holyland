/* libdev.a: sceDevVu0GetTop (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

unsigned short sceDevVu0GetTop(void)
{
    unsigned char buf[16] __attribute__((aligned(16)));

    sceDevVu0GetMem(buf, 0x43A);
    return *(unsigned short *)buf;
}
