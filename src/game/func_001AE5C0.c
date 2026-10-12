#include "kf_effect.h"

extern f32 D_0044616C;
extern f32 D_00446170[2];
extern f32 D_00431AF0[16];

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216778(void *, void *);
int rand(void);
void sceVu0RotMatrixZ(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Turns its heading by a random amount each frame and pushes the model along it toward the target. */
void func_001AE5C0(EffectActor *actor) {
    ReseedMotion *motion;
    f32 matrix[16];
    f32 direction[4];
    f32 angle;

    func_001AEC48(actor);
    motion = (ReseedMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    direction[0] = 1.0f;
    direction[1] = direction[2] = direction[3] = 0.0f;
    angle = motion->unk10 + actor->def->unkDC + ((rand() & 0xFFFF) * (1.0f / 32768.0f) - 1.0f) * actor->def->unkE4;
    angle -= (int)(angle / D_0044616C) * D_0044616C;
    if (D_00446170[0] < angle) {
        angle -= D_0044616C;
    } else if (angle < D_00446170[1]) {
        angle += D_0044616C;
    }
    motion->unk10 = angle;
    sceVu0RotMatrixZ(matrix, D_00431AF0, angle);
    sceVu0ApplyMatrix(direction, matrix, direction);
    actor->model->unkE0 += actor->def->unkE0 * direction[0];
    actor->model->unkE4 += actor->def->unkE0 * direction[1];
    func_00216778(&actor->model->position, &actor->target->model->position);
    actor->frame += 1;
}
