#include "kf_effect.h"

void func_00216778(void *, void *);

void func_001AA728(EffectActor *actor) {
    TurningMotion *turn = (TurningMotion *)&actor->motion;

    func_00216778(&actor->motion, &actor->target->model->position);
    func_00216778(&turn->unk10, &actor->target->model->rotation);
}
