#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
void func_00216778(void *, void *);
int rand(void);

/* Starts an effect at a random offset within its definition's range, and sets its two drift rates. */
void func_001AE060(EffectActor *actor, int frame) {
    DriftMotion *motion;

    motion = (DriftMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    func_00216778(&motion->end_position, &actor->model->position);
    motion->unk20 = (((f32)(rand() & 0xFFFF) * (1.0f / 32768.0f)) - 1.0f) * actor->def->unkE4;
    motion->unk24 = actor->def->unkE8;
}
