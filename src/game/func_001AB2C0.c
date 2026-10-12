#include "kf_effect.h"

extern char D_00413F80[];
void KF_Vector_Subtract(void *, void *, void *);
void func_001AFC88(float, float, void *, void *);
int func_001C0E40(float, void *);
int func_001B4F90(void *);
void func_001A2A18(void *);
int func_001A1E28(int, int, int, int, u16, int);
void sceVu0AddVector(void *, void *, void *);

/* Steps an effect that drops from the player's position toward the ground, resting once it lands. */
void func_001AB2C0(EffectActor *actor) {
    CountedMotion *motion;
    Vector4 offset;
    int hit;

    motion = (CountedMotion *)&actor->motion;
    if (actor->frame < actor->def->max_frames || actor->def->max_frames == 0) {
        KF_Vector_Subtract(&offset, D_00413F80, &actor->model->position);
        func_001AFC88(actor->def->speed, actor->def->unkDC, motion, &offset);
        actor->model->position.y -= actor->def->unkE0 * 0.5f;
        motion->velocity.w = 0;
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
        if (func_001C0E40(actor->def->unkE0, &actor->model->position) == 0) {
            actor->frame = actor->def->restart_frame;
            actor->model->position.y += actor->def->unkE0 * 0.5f;
        } else {
            actor->model->position.y = *(float *)0x70000004;
        }
    }
    if (motion->flag == 1) {
        func_001A2A18(actor);
    } else if (actor->frame == 0) {
        actor->model->position.y -= 100.0f;
        hit = func_001B4F90(actor);
        actor->model->position.y += 100.0f;
        if ((hit & 0x10) == 0) {
            if ((hit & 1) != 0) {
                func_001A1E28(actor->def->end_effect, (int)&actor->model->position, (int)&actor->model->rotation,
                              actor->effect_arg, actor->effect_param, (int)actor->owner);
                motion->flag = 1;
            }
        } else {
            motion->flag = 1;
        }
    }
    actor->frame += 1;
}
