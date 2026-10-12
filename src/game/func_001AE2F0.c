#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216C48(void *, void *, void *);
int rand(void);
void sceVu0ApplyMatrix(float *, float *, float *);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);

/* Scatters the velocity by a random pitch and yaw, bounces it off the ceiling height, and moves the model. */
void func_001AE2F0(EffectActor *actor) {
    f32 matrix[16];
    Vector4 *velocity;

    velocity = &actor->motion.velocity;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    sceVu0RotMatrixX(matrix, D_00431AF0, ((f32)(rand() & 0xFFFF) * 3.0517578125e-05f - 1.0f) * actor->def->unkDC);
    sceVu0RotMatrixY(matrix, matrix, ((f32)(rand() & 0xFFFF) * 3.0517578125e-05f - 1.0f) * actor->def->unkDC);
    sceVu0ApplyMatrix(&velocity->x, matrix, &velocity->x);
    if (actor->def->unkE0 < actor->model->position.y) {
        if (velocity->y > 0.0f) {
            velocity->y = -velocity->y;
        }
    }
    func_00216C48(&actor->model->position, &actor->model->position, velocity);
    actor->frame += 1;
}
