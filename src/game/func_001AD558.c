#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void sceVu0AddVector(void *, void *, void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);

void func_001AD558(EffectActor *actor) {
    EndingMotion *motion;

    func_001AEC48(actor);
    motion = (EndingMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
    if (motion->ended) {
        func_001A2A18(actor);
    } else if (actor->frame == motion->end_frame) {
        func_001A1E28(actor->def->end_effect, (int)&motion->end_position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
        motion->ended = TRUE;
    } else {
        func_001AF298(actor, &motion->ended);
    }
    actor->frame++;
}
