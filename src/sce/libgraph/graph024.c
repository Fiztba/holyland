/* libgraph.a member graph024.o (KFIV; not linked by ICO). sceGsSwapDBuffDc:
   sceGsSwapDBuff for the double-context buffer of graph023.c. Member number
   derived. */
#include <libgraph.h>

int sceGsSwapDBuffDc(void *db, int id)
{
    int s0 = id & 1;
    int ret;
    sceGsPutDispEnv((char *)db + s0 * 0x28);
    if (s0)
        ret = sceGsPutDrawEnv((char *)db + 0x1C0);
    else
        ret = sceGsPutDrawEnv((char *)db + 0x50);
    return ret;
}
