#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
int rand(void);

/* Starts an effect at a random point inside the box the definition describes, and lifts the model 220 units. */
void func_001A80E8(EffectActor *actor, int frame) {
    EndingMotion *motion;

    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    motion = (EndingMotion *)&actor->motion;
    motion->end_position.x = actor->def->origin_x + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_x;
    motion->end_position.y = actor->def->origin_y + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_y;
    motion->end_position.z = actor->def->origin_z + ((rand() & 0xFFFF) * (1.0f / 32768) - 1.0f) * actor->def->spread_z;
    motion->end_position.w = 0;
    motion->end_frame = -1;
    actor->model->position.y -= 220.0f;
    motion->ended = 0;
    actor->unk6C = 0;
    actor->flags |= (s64)0x8000 << 0x2B;
}
