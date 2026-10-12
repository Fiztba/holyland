#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);

/* Re-seeds the motion from the model and definition, lowers the model by 220 and clears the motion's state. */
void func_001A4DB8(EffectActor *actor, int frame) {
    ReseedMotion *motion;

    motion = (ReseedMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    actor->model->position.y -= 220.0f;
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion->unk14 = 0;
    motion->unk10 = 0.0f;
}
