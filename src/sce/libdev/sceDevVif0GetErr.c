/* libdev.a: sceDevVif0GetErr (KFIV: not in ICO; the name is inferred from what the
   code does, not taken from SDK headers) */
#include <libdev_internal.h>

unsigned int sceDevVif0GetErr(void)
{
    return LIBDEV_REG32(VIF0_ERR);
}
