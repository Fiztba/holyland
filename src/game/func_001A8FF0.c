#include "common.h"
#include "kf_effect.h"

void KF_Vector_Subtract(Vector4 *, void *, void *);
void func_001AF5B0(void *, void *);
float func_001B0178(void *, void *);
f32 func_001B0950(f32);
void func_00216778(void *, void *);

void func_001A8FF0(EffectActor *actor, Vector4 *target) {
    Vector4 vec;
    HeadingMotion *motion;

    motion = (HeadingMotion *) &actor->motion;
    KF_Vector_Subtract(&vec, target, &actor->model->position);
    func_001AF5B0(&actor->model->rotation, &vec);
    motion->heading = (s16) func_001B0950((func_001B0178(target, &actor->model->position) - (f32) actor->def->radius_half) / 500.0f);
    func_00216778(&actor->motion.unk10, target);
}
