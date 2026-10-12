#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void func_001AF538(float *, float *, float);
void func_001AF5B0(void *, void *);
void func_001A2A18(void *);
void func_0019CE30(int, void *);
void func_00216778(void *, void *);
void KF_Vector_Subtract(Vector4 *, Vector4 *, Vector4 *);
void sceVu0AddVector(void *, void *, void *);
extern float D_00413F80[];

/* Turns toward a point until turn_end_frame; then moves by the velocity until move_end_frame + 0x3C. */
void func_001AC8E8(EffectActor *actor) {
    PhasedMotion *motion;
    Vector4 turn;
    Vector4 target;

    func_001AEC48(actor);
    motion = (PhasedMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    if (actor->frame >= motion->move_end_frame) {
        if (actor->frame == motion->move_end_frame) {
            func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
            func_0019CE30((u8)(int)actor->def->unkE0, &actor->model->position);
        }
        sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
        func_001AF298(actor, actor->pad00 + 0x34);
        if (motion->move_end_frame + 0x3C < actor->frame) {
            func_001A2A18(actor);
        }
    } else if (actor->frame < motion->turn_end_frame) {
        func_00216778(&target, D_00413F80);
        target.y -= 110.0f;
        KF_Vector_Subtract(&turn, &target, &actor->model->position);
        func_001AF5B0(&actor->model->rotation, &turn);
    }
    actor->frame++;
}
