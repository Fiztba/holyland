#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Sets the effect's frame count to its definition's limit less the caller's step count. */
void func_001A99D8(EffectActor *actor, TurningMotion *motion) {
    func_001AF928(actor, (int)motion);
    func_001AF640(actor, (int)motion);
    *(s32 *)&actor->motion.velocity.x = actor->def->unkA6 - motion->unk25;
}
