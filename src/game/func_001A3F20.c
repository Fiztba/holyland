#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void func_001AF538(float *, float *, float);
void func_00216778(void *, void *);
void KF_Vector_Scale(float *, float *, float);
void sceVu0AddVector(void *, void *, void *);

/* Moves the effect along a velocity made from its model's heading, optionally accelerating and scaling it. */
void func_001A3F20(EffectActor *actor) {
    ScaledMotion *motion;
    Vector4 step;

    motion = (ScaledMotion *)&actor->motion;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        if (EFFECT_DEF_FLAGS(actor->def)->flags & EFFECT_DEF_FOLLOWS_HEADING) {
            func_001AF538(&step.x, &actor->model->rotation.x, actor->def->speed);
            motion->scale += actor->def->unk11C;
            KF_Vector_Scale(&step.x, &step.x, motion->scale);
        } else {
            if (actor->def->gravity != 0.0f) {
                motion->fall_speed += actor->def->gravity;
                motion->velocity.y += motion->fall_speed;
            }
            func_00216778(&step, motion);
            if (actor->def->unk11C != 0.0f) {
                motion->scale += actor->def->unk11C;
                step.x *= motion->scale;
                step.z *= motion->scale;
            }
        }
        sceVu0AddVector(&actor->model->position, &actor->model->position, &step);
    }
    func_001AF298(actor, motion->unk1C);
    actor->frame++;
}
