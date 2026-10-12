#include "kf_effect.h"

int func_001AEC48(void *);
int func_001AF0B0(void *);
void func_0019B1E8(f32, s32, void *);
void func_00216778(void *, void *);

/* Runs the effect update, then copies the motion to and from the model and target positions. */
void func_001AD6A8(EffectActor *actor) {
    EffectMotion *motion;

    motion = &actor->motion;
    func_001AEC48(actor);
    func_001AF0B0(actor);
    func_0019B1E8(actor->model->unkE8 * 100.0f, EFFECT_FLAG_WORDS(actor)->unk44, actor);
    func_00216778(&actor->model->position, motion);
    func_00216778(motion, &actor->target->model->position);
    actor->frame += 1;
}
