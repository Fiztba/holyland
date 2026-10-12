#include "common.h"
#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void sceVu0AddVector(void *, void *, void *);

void func_001A9E08(EffectActor *actor) {
    EffectMotion *motion;

    func_001AEC48(actor);
    motion = &actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
    }
    actor->frame += 1;
}
