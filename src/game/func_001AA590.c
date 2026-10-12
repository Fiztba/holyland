#include "kf_effect.h"

extern void (*D_00402550[][2])();

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
int func_001B4F90(void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);
void func_001AA3E0(EffectActor *actor, int frame);
void func_001AA438(EffectActor *actor, int frame);
void sceVu0AddVector(void *, void *, void *);

/* Moves by its velocity; once its frame passes end_frame it spawns its effect, or earlier does so on a hit. */
void func_001AA590(EffectActor *actor) {
    DelayedMotion *motion;
    int hit;

    func_001AEC48(actor);
    motion = (DelayedMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
    if (motion->ended != 0) {
        func_001A2A18(actor);
    } else if (motion->end_frame < actor->frame) {
        func_001A1E28(motion->effect_id, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
        motion->ended = 1;
    } else if (actor->def->unk10B != 0xFF) {
        hit = func_001B4F90(actor);
        if (hit != 0 && actor->def->end_effect != 0xFFFF) {
            if (hit & 2) {
                func_001A1E28(actor->def->end_effect, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
            } else if ((D_00402550[actor->def->unk100][0] == func_001AA3E0 || D_00402550[actor->def->unk100][0] == func_001AA438) && (*(int *)0x70000018 == 3 || *(int *)0x70000018 == 5)) {
                func_001A1E28(actor->def->interval, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
            } else {
                func_001A1E28(motion->effect_id, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
            }
            motion->ended = 1;
        }
    }
    actor->frame++;
}
