#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void sceVu0AddVector(void *, void *, void *);
void func_001A2A18(void *);
void func_001A2BC8(int);
int func_001A1E28(int, int, int, int, u16, int);

/* Moves the model; after a countdown it ends, otherwise it spawns the end effect (re-armed each interval). */
void func_001A8ED0(EffectActor *actor) {
    CountdownMotion *motion;
    s8 spawned = FALSE;

    func_001AEC48(actor);
    motion = (CountdownMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (motion->countdown > 0) {
        motion->countdown--;
        if (motion->countdown <= 0) {
            func_001A2A18(actor);
            return;
        }
    } else {
        if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
            sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
        }
        if (actor->frame >= motion->end_frame) {
            func_001A1E28(actor->def->end_effect, (int)&motion->end_position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
            spawned = TRUE;
        } else {
            func_001AF298(actor, &spawned);
        }
        if (spawned) {
            func_001A2BC8((int)actor);
            motion->countdown = actor->def->interval;
        }
    }
    actor->frame++;
}
