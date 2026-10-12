#include "common.h"
#include "kf_effect.h"

extern f32 D_00431AF0[16];

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_00216778(void *, void *);
void func_00216C48(void *, void *, void *);
void sceVu0AddVector(void *, void *, void *);
void sceVu0RotMatrixX(float *, float *, float);
void sceVu0RotMatrixY(float *, float *, float);
void sceVu0RotMatrixZ(float *, float *, float);
void sceVu0ApplyMatrix(float *, float *, float *);

/* Moves the model by the velocity (with gravity) while the frame is in range, then turns the offset by the model's rotation. */
void func_001AAD40(EffectActor *actor) {
    f32 matrix[16];
    Vector4 offset;
    DriftMotion *motion;
    EffectDef *def;

    motion = (DriftMotion *)&actor->motion;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    func_00216778(&actor->model->position, actor->start_position);
    func_00216778(&actor->model->rotation, actor->start_rotation);
    def = actor->def;
    if (actor->frame < def->max_frames || def->max_frames == 0) {
        if (def->gravity != 0.0f) {
            motion->unk24 += def->gravity;
        }
        sceVu0AddVector(motion, motion, &motion->unk20);
    }
    sceVu0RotMatrixZ(matrix, D_00431AF0, actor->model->rotation.z);
    sceVu0RotMatrixX(matrix, matrix, actor->model->rotation.x);
    sceVu0RotMatrixY(matrix, matrix, actor->model->rotation.y);
    sceVu0ApplyMatrix(&offset.x, matrix, &motion->velocity.x);
    func_00216C48(&actor->model->position, &actor->model->position, &offset);
    func_00216C48(&actor->model->rotation, &actor->model->rotation, &motion->end_position);
    actor->frame += 1;
    if (actor->def->radius_half != 0 && actor->frame >= actor->def->restart_frame) {
        actor->frame = 0;
    }
}
