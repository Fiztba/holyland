#include "kf_effect.h"

void func_001AA3E0(EffectActor *actor, int frame);

/* Runs the turning update, then sets flag bit 58 (EffectFlagBits.pad58). */
void func_001AA438(EffectActor *actor, int frame) {
    func_001AA3E0(actor, frame);
    actor->flags |= (s64)0x8000 << 43;
}
