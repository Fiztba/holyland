#include "common.h"
#include "kf_effect.h"

int func_001AEC48(void *);
int func_001AF0B0(void *);
void func_0019B1E8(f32, s32, void *);

void func_001AD7A0(EffectActor *actor) {
    func_001AEC48(actor);
    func_001AF0B0(actor);
    func_0019B1E8(actor->model->unkE8 * 100.0f, EFFECT_FLAG_WORDS(actor)->unk44, actor);
    actor->frame += 1;
}
