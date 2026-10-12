#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);

/* Re-seeds the motion from the model and definition, lifts the model 220 units, and sets bit 58 of the flags. */
void func_001A52B8(EffectActor *actor, int frame) {
    LiftedMotion *motion;
    ModelInstance *model;

    motion = (LiftedMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    model = actor->model;
    model->position.y -= 220.0f;
    func_001AF538(&motion->velocity.x, &model->rotation.x, actor->def->speed);
    motion->unk10 = 0;
    actor->flags |= (s64)0x8000 << 0x2B;
}
