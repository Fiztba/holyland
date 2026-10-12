#include "common.h"
#include "kf_effect.h"

extern f32 D_0044610C;
extern f32 D_00446110[2];
extern f32 D_00431AF0[16];

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216778(void *, void *);
void sceVu0RotMatrixZ(void *, void *, float);
void sceVu0ApplyMatrix(void *, void *, void *);

/* Turns its heading by a fixed step and moves the model that way, then follows the target's position. */
void func_001AC2C0(EffectActor *actor) {
    f32 matrix[16];
    Vector4 direction;
    ReseedMotion *motion;
    f32 heading;

    func_001AEC48(actor);
    motion = (ReseedMotion *)&actor->motion;
    func_001AED58(actor);
    func_001AF0B0(actor);
    direction.y = direction.z = direction.w = 0.0f;
    direction.x = 1.0f;
    heading = motion->unk10 + actor->def->unkDC;
    heading = heading - (int)(heading / D_0044610C) * D_0044610C;
    if (D_00446110[0] < heading) {
        heading -= D_0044610C;
    } else if (heading < D_00446110[1]) {
        heading += D_0044610C;
    }
    motion->unk10 = heading;
    sceVu0RotMatrixZ(matrix, D_00431AF0, heading);
    sceVu0ApplyMatrix(&direction, matrix, &direction);
    actor->model->unkE0 += actor->def->unkE0 * direction.x;
    actor->model->unkE4 += actor->def->unkE0 * direction.y;
    func_00216778(&actor->model->position, &actor->target->model->position);
    actor->frame++;
}
