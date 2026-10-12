#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
s16 func_001B0208(float, void *, void *, void *);

void func_001A8E48(EffectActor *actor, int frame) {
    TurningMotion *motion;

    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&actor->motion.velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion = (TurningMotion *)&actor->motion;
    motion->unk20 = func_001B0208((float)actor->def->radius_half, actor, motion, motion->unk10);
    ((ArmedMotion *)motion)->unk22 = 0;
}
