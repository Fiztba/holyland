#include "kf_effect.h"

int func_001AEC48(EffectActor *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void KF_Vector_Subtract(void *, void *, void *);
void func_001AFC88(float, float, void *, void *);
void *memset(void *, int, u32);
void sceVu0AddVector(void *, void *, void *);
extern u8 D_0034A019[];

void func_001AE970(EffectActor *actor) {
    f32 scratch[4];
    f32 offset[4];
    EffectMotion *motion = &actor->motion;

    memset(scratch, 0, 0x10);
    scratch[1] = -270.0f;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        if (D_0034A019[0] != 0 && actor->def->radius_half < actor->frame) {
            KF_Vector_Subtract(offset, scratch, &actor->model->position);
            func_001AFC88(actor->def->speed, actor->def->unkDC, motion, offset);
        }
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
    }
    actor->frame += 1;
}
