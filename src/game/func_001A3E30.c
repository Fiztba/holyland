#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);

/* Re-seeds a falling effect's motion from its model and definition. */
void func_001A3E30(EffectActor *actor, int frame) {
    ScaledMotion *motion;

    motion = (ScaledMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion->unk1C[0] = 0;
    motion->fall_speed = 0.0f;
    motion->scale = 1.0f;
}
