/* libgraph.a member graph025.o (KFIV; not linked by ICO). sceGsSetDefDrawEnv2:
   graph006.c's sceGsSetDefDrawEnv for drawing context 2. Member number
   derived. */
#include <libgraph.h>

typedef long long s_long128;

/* graph006's sceGsSetDefDrawEnv for drawing context 2: FRAME_2, ZBUF_2,
   XYOFFSET_2, SCISSOR_2 and TEST_2 instead of the context-1 addresses. */
int sceGsSetDefDrawEnv2(sceGsDrawEnv *env, short psm, short w, short h, short ztst, short zpsm)
{
    env->frame_addr = 0x4D;
    env->frame = ((s_long128)((w >> 6) & 0x3F) << 16) | ((s_long128)(psm & 0xF) << 24);
    env->zbuf_addr = 0x4F;
    if (ztst == 0) {
        env->zbuf = (s_long128)(short)sceGszbufaddr(psm, w, h) | ((s_long128)(zpsm & 0xF) << 24) |
                    ((s_long128)1 << 32);
    } else {
        env->zbuf = (s_long128)(short)sceGszbufaddr(psm, w, h) | ((s_long128)(zpsm & 0xF) << 24);
    }
    env->xyoffset_addr = 0x19;
    env->xyoffset =
        (((s_long128)0x800 - (short)(w >> 1)) << 4) | (((s_long128)0x800 - (short)(h >> 1)) << 36);
    env->scissor_addr = 0x41;
    env->scissor = ((s_long128)(w - 1) << 16) | ((s_long128)(h - 1) << 48);
    env->prmodecont_addr = 0x1A;
    env->prmodecont |= 1;
    env->colclamp_addr = 0x46;
    env->colclamp |= 1;
    env->dthe_addr = 0x45;
    if (psm & 2) {
        env->dthe |= 1;
    } else {
        env->dthe &= ~1;
    }
    env->test_addr = 0x48;
    if (ztst) {
        env->test = ((s_long128)(ztst & 3) << 17) | 0x00010000;
    } else {
        env->test = 0x00030000;
    }
    __asm__ __volatile__("sync");
    return 8;
}

