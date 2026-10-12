#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);

/* Re-seeds the motion from the model and definition, then sets the float at 0x10 from the definition. */
void func_001A6A20(EffectActor *actor, int frame) {
    ReseedMotion *motion;

    motion = (ReseedMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion->unk10 = actor->def->unkDC;
    motion->unk14 = 0;
}
