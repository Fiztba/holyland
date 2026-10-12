#include "kf_effect.h"

int func_001A2928(int effect_id, EffectActor *actor, Vector4 *position);
void func_00216C48(void *, void *, void *);
void sceVu0RotMatrixY(void *d, void *s, float a);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
extern f32 D_00431AF0[];

/* Spawns effect_id at the point distance ahead of the actor's model, along its heading. */
void func_001A6498(EffectActor *actor, int effect_id, f32 distance) {
    Vector4 offset;
    Vector4 position;
    f32 rotation[4][4];

    offset.x = 0.0f;
    offset.y = 0.0f;
    offset.z = distance;
    offset.w = 0.0f;
    sceVu0RotMatrixY(rotation, D_00431AF0, actor->model->rotation.y);
    sceVu0ApplyMatrix(&position, rotation, &offset);
    func_00216C48(&position, &position, &actor->model->position);
    func_001A2928(effect_id, actor, &position);
}
