#include "common.h"
#include "kf_effect.h"

void func_001AF640(void *, int);
void func_00216778(void *, void *);
void func_001AF538(float *, float *, float);
int rand(void);

/* Picks a random start point and aim point inside the definition's two boxes and sets its velocity. */
void func_001AABA0(EffectActor *actor, int frame) {
    ScatterMotion *motion = (ScatterMotion *)&actor->motion;

    func_001AF640(actor, frame);
    func_00216778(&actor->model->rotation, actor->start_rotation);
    motion->start.x = actor->def->origin_x + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_x;
    motion->start.y = actor->def->origin_y + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_y;
    motion->start.z = actor->def->origin_z + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_z;
    motion->start.w = 0;
    motion->aim.x = actor->def->start_x + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->start_spread_x;
    motion->aim.y = actor->def->start_y + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->start_spread_y;
    motion->aim.z = actor->def->start_z + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->start_spread_z;
    motion->aim.w = 0;
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
}
