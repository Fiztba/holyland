#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void func_00216778(void *, void *);
void sceVu0AddVector(void *, void *, void *);

/* Steps the effect: moves the model by its velocity (scaled by a growing factor), then counts a frame. */
void func_001ABE20(EffectActor *actor) {
    Vector4 offset;
    ReseedMotion *motion;
    EffectDef *def;
    f32 scale;

    func_001AEC48(actor);
    motion = (ReseedMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        func_00216778(&offset, motion);
        def = actor->def;
        if (def->unk11C != 0.0f) {
            scale = motion->unk10 + def->unk11C;
            motion->unk10 = scale;
            offset.x *= scale;
            offset.z *= scale;
        }
        sceVu0AddVector(&actor->model->position, &actor->model->position, &offset);
    }
    func_001AF298(actor, &motion->unk14);
    actor->frame += 1;
}
