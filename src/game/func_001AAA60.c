#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Re-seeds the actor's velocity x and y from its definition. */
void func_001AAA60(EffectActor *actor, int frame) {
    EffectMotion *motion;
    EffectDef *def;

    motion = &actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    def = actor->def;
    motion->velocity.x = def->unkDC;
    motion->velocity.y = def->unkE0;
}
