#include "kf_effect.h"

void func_001A3F20(EffectActor *);
void KF_Vector_Scale(float *, float *, float);

/* Advances the effect one frame, then scales the model's unkE0 vector by the motion's float. */
void func_001A40E8(EffectActor *actor) {
    FollowerMotion *motion;

    motion = (FollowerMotion *)&actor->motion;
    func_001A3F20(actor);
    KF_Vector_Scale(&actor->model->unkE0, &actor->model->unkE0, motion->unk18);
}
