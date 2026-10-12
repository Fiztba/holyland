#include "kf_effect.h"
#include "kf_entity.h"

#define SCRATCH_VECTOR ((f32 *)0x70000000)

float func_001B0178(void *, void *);
int func_001C0E40(float, void *);
void func_001AFC88(float, float, void *, void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);
void func_00216778(void *, void *);
void KF_Vector_Subtract(void *, void *, void *);
void sceVu0AddVector(void *, void *, void *);

/* Drifts toward the entity it was aimed at and ends when it gets within range. */
void func_001A50E8(EffectActor *actor) {
    SeekingMotion *motion = (SeekingMotion *)&actor->motion;
    f32 vec[4];

    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        if (actor->unk6C != 0) {
            if (((u32)(((Entity *)actor->unk6C)->flags >> 2) & 3) == 0) {
                KF_Vector_Subtract(vec, ((Entity *)actor->unk6C)->model->position, &actor->model->position);
                func_001AFC88(actor->def->speed, actor->def->unkDC, motion, vec);
            } else {
                actor->unk6C = 0;
            }
        }
        actor->model->position.y -= actor->def->unkE0 * 0.5f;
        motion->velocity.w = 0;
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
        if (func_001C0E40(actor->def->unkE0, &actor->model->position) == 0) {
            actor->frame = actor->def->restart_frame;
            actor->model->position.y += actor->def->unkE0 * 0.5f;
        } else {
            actor->model->position.y = SCRATCH_VECTOR[1];
        }
    }
    if (motion->state == 1) {
        func_001A2A18(actor);
    } else if (actor->frame == 0) {
        func_00216778(vec, &actor->model->position);
        if (actor->unk6C != 0 && func_001B0178(vec, ((Entity *)actor->unk6C)->model->position) < 100.0f) {
            func_001A1E28(actor->def->end_effect, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
            motion->state = 1;
        }
    }
    actor->frame += 1;
}
