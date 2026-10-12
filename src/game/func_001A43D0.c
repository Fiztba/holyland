#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Resets an effect actor's target link after re-seeding its motion. */
void func_001A43D0(EffectActor *actor, int frame) {
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    actor->target = NULL;
}
