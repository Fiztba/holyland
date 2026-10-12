#include "common.h"
#include "kf_effect.h"

extern int D_004468E8;

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
void func_001AF5B0(void *, void *);
void func_00216778(void *, void *);

/* Turns the model's heading by a step per spawn, then records the spawn index and scale. */
void func_001AB9C0(EffectActor *actor, int frame) {
    Vector4 rotation;
    SpawnedMotion *motion;
    f32 heading;

    motion = (SpawnedMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_00216778(&rotation, &actor->model->rotation);
    rotation.y += D_004468E8 * actor->def->unkDC;
    heading = rotation.y;
    heading -= (int)(heading / 6.2831855f) * 6.2831855f;
    if (3.1415927f < heading) {
        heading -= 6.2831855f;
    } else if (heading < -3.1415927f) {
        heading += 6.2831855f;
    }
    rotation.y = heading;
    func_001AF538(&motion->velocity.x, &rotation.x, actor->def->speed);
    func_001AF5B0(&actor->model->rotation, motion);
    motion->velocity.y -= actor->def->radius_half;
    motion->flag = 0;
    motion->spawn_index = D_004468E8;
    motion->scale = 1.0f;
    D_004468E8++;
}
