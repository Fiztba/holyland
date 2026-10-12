#include "common.h"
#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);

void func_001A9A28(EffectActor *actor) {
    SnappedMotion *motion;
    EffectDef *def;
    ModelInstance *model;
    f32 ratio;

    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    motion = (SnappedMotion *) &actor->motion;
    def = actor->def;
    model = actor->model;
    ratio = (f32) motion->radius_half / (f32) def->unkA6;
    actor->frame += 1;
    model->unkE4 = def->unkDC + model->unkE4 * ratio;
}
