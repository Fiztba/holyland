#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
s16 func_001B0208(float, void *, void *, void *);

void func_001A4C50(EffectActor *actor, int frame) {
    ProjectileMotion *motion;
    char scratch[0x10];

    motion = (ProjectileMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    actor->model->position.y -= 220.0f;
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion->end_frame = func_001B0208((float)actor->def->radius_half, actor, motion, scratch);
}
