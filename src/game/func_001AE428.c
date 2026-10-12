#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_00216778(void *, void *);

/* Re-seeds the motion, then copies the target's model position and rotation into it. */
void func_001AE428(EffectActor *actor, int frame) {
    EffectMotion *motion;

    motion = &actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_00216778(&motion->velocity, &actor->target->model->position);
    func_00216778(&motion->unk10, &actor->target->model->rotation);
}
