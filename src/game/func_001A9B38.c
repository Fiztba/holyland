#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];
int rand(void);
void func_001AF640(void *, int);
void func_00216C48(void *, void *, void *);
void func_00216D20(void *, void *);
void KF_Vector_Scale(float *, float *, float);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Points the model in a random direction, steps it out by the radius and sets the motion back along it. */
void func_001A9B38(EffectActor *actor, int frame) {
    f32 matrix[16];
    Vector4 direction;
    EffectMotion *motion;

    motion = &actor->motion;
    func_001AF640(actor, frame);
    direction.x = direction.y = direction.w = 0.0f;
    direction.z = actor->def->radius_half;
    actor->model->rotation.x = ((rand() & 0xFFFF) * 0.000030517578f - 1.0f) * 3.1415f;
    actor->model->rotation.y = ((rand() & 0xFFFF) * 0.000030517578f - 1.0f) * 3.1415f;
    actor->model->rotation.w = actor->model->rotation.z = 0.0f;
    sceVu0RotMatrixX(matrix, D_00431AF0, actor->model->rotation.x);
    sceVu0RotMatrixY(matrix, matrix, actor->model->rotation.y);
    sceVu0ApplyMatrix(&direction.x, matrix, &direction.x);
    func_00216C48(&actor->model->position, &actor->model->position, &direction);
    func_00216D20(&direction, &direction);
    KF_Vector_Scale(&motion->velocity.x, &direction.x, -actor->def->speed);
}
