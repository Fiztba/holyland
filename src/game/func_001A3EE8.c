#include "kf_effect.h"

void func_001A3E30(EffectActor *actor, int frame);

/* Re-seeds the actor's motion, then sets bit 58 of its flags. */
void func_001A3EE8(EffectActor *actor, int frame) {
    func_001A3E30(actor, frame);
    actor->flags |= (s64)0x8000 << 43;
}
