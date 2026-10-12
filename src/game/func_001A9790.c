#include "common.h"
#include "kf_effect.h"

extern int D_004468E0;
extern f32 D_004460D0;
extern f32 D_004460D4[2];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
void func_001AF5B0(void *, void *);
void func_00216778(void *, void *);

/* Turns the model's heading by a step per spawn, then records the spawn index and phase. */
void func_001A9790(EffectActor *actor, int frame) {
    Vector4 rotation;
    RippleMotion *motion;
    f32 heading;

    motion = (RippleMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_00216778(&rotation, &actor->model->rotation);
    rotation.y += D_004468E0 * actor->def->unkDC;
    heading = rotation.y;
    heading -= (int)(heading / D_004460D0) * D_004460D0;
    if (D_004460D4[0] < heading) {
        heading -= D_004460D0;
    } else if (heading < D_004460D4[1]) {
        heading += D_004460D0;
    }
    rotation.y = heading;
    func_001AF538(&motion->velocity.x, &rotation.x, actor->def->speed);
    func_001AF5B0(&actor->model->rotation, motion);
    motion->velocity.y -= actor->def->radius_half;
    motion->flag = 0;
    motion->spawn_index = D_004468E0;
    motion->phase = D_004468E0 % actor->def->interval;
    D_004468E0++;
}
