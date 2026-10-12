#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
int rand(void);

/* Re-seeds the spawner motion from the definition and restarts its count. */
void func_001A44D8(EffectActor *actor, int frame) {
    SpawnerMotion *motion;
    EffectDef *def;

    motion = (SpawnerMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    motion->count = 0;
    def = actor->def;
    motion->effect_id = def->radius_half;
    motion->repeat_limit = def->interval;
    motion->interval_base = def->unkF0;
    motion->interval_range = def->unkF2;
    motion->next_frame = motion->interval_base + rand() % motion->interval_range;
}
