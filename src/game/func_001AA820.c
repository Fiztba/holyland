#include "kf_effect.h"

void func_001AF928(void *, int);
void func_00216778(void *, void *);

void func_001AA820(EffectActor *actor, int frame) {
    EffectMotion *motion = &actor->motion;

    func_001AF928(actor, frame);
    func_00216778(motion, &actor->target->model->position);
}
