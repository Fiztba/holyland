#include "common.h"
#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
int func_001B4F90(void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);
void func_001AF5B0(void *, void *);
void sceVu0AddVector(void *, void *, void *);

/* Moves by its velocity (and gravity) until it hits, then spawns two effects and ends. */
void func_001A4E38(EffectActor *actor) {
    EndedMotion *motion = (EndedMotion *)&actor->motion;
    u16 effect_id;

    if (motion->ended != 0) {
        func_001A2A18(actor);
        return;
    }
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        if (actor->def->gravity != 0.0f) {
            motion->velocity.y += actor->def->gravity;
        }
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
        func_001AF5B0(&actor->model->rotation, motion);
    }
    if (func_001B4F90(actor) != 0) {
        effect_id = 0x16;
        if (*(int *)0x70000018 != 5) {
            effect_id = actor->def->end_effect;
        }
        func_001A1E28(effect_id, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
        func_001A1E28(0x122, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
        motion->ended = 1;
    }
    actor->frame++;
}
