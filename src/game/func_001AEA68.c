#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

void func_001AEA68(EffectActor *actor, int frame) {
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    actor->target = NULL;
    actor->motion.velocity.x = 0.0f;
}
