#include "kf_effect.h"

void func_001AAEA0(EffectActor *, int);

/* Runs func_001AAEA0 on the effect, then sets flag bit 58 of its flags word. */
void func_001AAF20(EffectActor *actor, int extra) {
    func_001AAEA0(actor, extra);
    actor->flags |= (s64)0x8000 << 43;
}
