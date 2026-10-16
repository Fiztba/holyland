/* libdev.a: sceDevVif1GetErr (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

unsigned int sceDevVif1GetErr(void)
{
    return LIBDEV_REG32(VIF1_ERR);
}
