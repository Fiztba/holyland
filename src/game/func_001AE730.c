#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_00216778(void *, void *);

/* Re-seeds the motion from the model and definition, then sets the two scale floats after the end position. */
void func_001AE730(EffectActor *actor, int frame) {
    DriftMotion *motion;

    motion = (DriftMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_00216778(&motion->end_position, &actor->model->position);
    motion->velocity.z = actor->def->unkE0;
    motion->velocity.x = motion->velocity.y = motion->velocity.w = 0.0f;
    motion->unk20 = 0.1f;
    motion->unk24 = 0.1f;
}
