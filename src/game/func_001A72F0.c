#include "common.h"
#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);

/* Scales the model's float at +0xE8 each frame and spawns the end effect on frame radius_half. */
void func_001A72F0(EffectActor *actor) {
    ScalingEndMotion *motion;

    motion = (ScalingEndMotion *)&actor->motion;
    if (motion->ended) {
        func_001A2A18(actor);
        return;
    }
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    actor->model->unkE8 *= motion->scale_per_mille / 1000.0f;
    if (actor->frame == actor->def->radius_half) {
        func_001A1E28(actor->def->end_effect, (int)&motion->end_position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
    }
    actor->frame++;
}
