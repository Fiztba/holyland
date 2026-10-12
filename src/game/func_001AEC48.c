#include "kf_effect.h"

int func_001AFBA8(void *, int *);
void func_001AFC08(f32 *, f32 *, int, int, int, int);
void func_00216778(void *, void *);
void sceVu0ScaleVectorXYZ(f32, void *, void *);
void sceVu0AddVector(void *, void *, void *);

/* Sets the model's offset from the keyframe the effect's frame falls in, or snaps to one. */
int func_001AEC48(EffectActor *actor) {
    Vector4 first;
    Vector4 second;
    int frames[4];
    f32 weight_first;
    f32 weight_second;
    int segment;

    frames[0] = 0;
    frames[1] = actor->def->unk90;
    frames[2] = actor->def->unk92;
    frames[3] = actor->def->restart_frame;
    segment = func_001AFBA8(actor, frames);
    if (frames[segment + 1] - frames[segment] <= 0) {
        func_00216778(&actor->model->unkE0, &actor->def->keys[segment]);
        return segment;
    }
    func_001AFC08(&weight_first, &weight_second, actor->frame, frames[segment], frames[segment + 1], actor->def->curve[segment]);
    sceVu0ScaleVectorXYZ(weight_first, &first, &actor->def->keys[segment]);
    sceVu0ScaleVectorXYZ(weight_second, &second, &actor->def->keys[segment + 1]);
    sceVu0AddVector(&actor->model->unkE0, &first, &second);
    return -1;
}
