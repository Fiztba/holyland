#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];
int rand(void);
void func_001AF640(void *, int);
void func_001AF5B0(void *, void *);
void func_00216778(void *, void *);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Aims the velocity from the target's rotation, jitters the model heading and copies the target's position. */
void func_001A8550(EffectActor *actor, int frame) {
    f32 matrix[16];
    Vector4 direction;
    Vector4 angles;
    EffectMotion *motion = &actor->motion;

    func_001AF640(actor, frame);
    direction.x = direction.z = direction.w = 0.0f;
    direction.y = 1.0f;
    func_001AF5B0(&angles, &actor->target->motion);
    sceVu0RotMatrixX(matrix, D_00431AF0, angles.x);
    sceVu0RotMatrixY(matrix, matrix, angles.y);
    sceVu0ApplyMatrix(&direction.x, matrix, &direction.x);
    func_001AF5B0(&actor->model->rotation, &direction);
    actor->model->rotation.z += actor->def->start_z + (((rand() & 0xFFFF) * 0.000030517578f) - 1.0f) * actor->def->start_spread_z;
    func_00216778(motion, &actor->target->model->position);
    func_00216778(&actor->motion.unk10, &actor->target->model->rotation);
}
