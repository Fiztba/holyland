#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];
int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216C48(void *, void *, void *);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);
void sceVu0RotMatrixZ(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Advances a tilt and a turn angle, and swings the end position around the model by them. */
void func_001AE118(EffectActor *actor) {
    Vector4 offset;
    f32 matrix[16];
    DriftMotion *motion;
    f32 angle;

    motion = (DriftMotion *)&actor->motion;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    motion->unk24 += actor->def->unkE0;
    offset.y = 0;
    offset.z = 0;
    offset.w = 0;
    offset.x = motion->unk24;
    angle = motion->unk20 + actor->def->unkDC;
    angle = angle - (f32)(s32)(angle / 6.2831855f) * 6.2831855f;
    if (3.1415927f < angle) {
        angle -= 6.2831855f;
    } else if (angle < -3.1415927f) {
        angle += 6.2831855f;
    }
    motion->unk20 = angle;
    sceVu0RotMatrixZ(matrix, D_00431AF0, angle);
    sceVu0RotMatrixX(matrix, matrix, actor->model->rotation.x);
    sceVu0RotMatrixY(matrix, matrix, actor->model->rotation.y);
    sceVu0ApplyMatrix(&offset.x, matrix, &offset.x);
    func_00216C48(&motion->end_position, &motion->end_position, motion);
    func_00216C48(&actor->model->position, &motion->end_position, &offset);
    actor->frame += 1;
}
