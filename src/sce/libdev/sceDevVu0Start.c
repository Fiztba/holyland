/* libdev.a: sceDevVu0Start (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

void sceDevVu0Start(unsigned int addr)
{
    LIBDEV_CTC2((addr & 0xFFFF), vi27);
    __asm__ __volatile__("vcallmsr $vi27");
}
