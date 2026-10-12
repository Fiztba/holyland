#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];
int rand(void);
int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216C48(void *, void *, void *);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Wobbles its two tilt angles within limits and rotates the velocity by them. */
void func_001AE7B0(EffectActor *actor) {
    f32 matrix[16];
    DriftMotion *motion;

    func_001AEC48(actor);
    motion = (DriftMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    motion->unk20 += ((rand() & 0xFFFF) * 0.000030517578f - 1.0f) * actor->def->unkDC;
    motion->unk24 += ((rand() & 0xFFFF) * 0.000030517578f - 1.0f) * actor->def->unkDC;
    if (motion->unk20 > 0.15f) {
        motion->unk20 = 0.15f;
    }
    if (motion->unk24 > 0.15f) {
        motion->unk24 = 0.15f;
    }
    if (motion->unk20 < -0.15f) {
        motion->unk20 = -0.15f;
    }
    if (motion->unk24 < -0.15f) {
        motion->unk24 = -0.15f;
    }
    sceVu0RotMatrixX(matrix, D_00431AF0, motion->unk20);
    sceVu0RotMatrixY(matrix, matrix, motion->unk24);
    sceVu0ApplyMatrix(&motion->velocity.x, matrix, &motion->velocity.x);
    func_00216C48(&actor->model->position, &motion->end_position, &motion->velocity);
}
