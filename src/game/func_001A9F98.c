#include "kf_effect.h"

int func_001A2928(int effect_id, EffectActor *actor, Vector4 *position);

/* Advances the frame counter into the def range, then spawns effect_id at position. */
void func_001A9F98(EffectActor *actor, Vector4 *position, int effect_id) {
    StepMotion *motion;
    EffectDef *def;
    int frame;

    motion = (StepMotion *)&actor->motion;
    def = actor->def;
    frame = def->unkF0;
    if (actor->frame < def->interval) {
        frame += def->interval - actor->frame;
    }
    motion->unk26 = frame;
    actor->frame = (char)frame;
    func_001A2928(effect_id, actor, position);
    motion->unk25 = 1;
}
