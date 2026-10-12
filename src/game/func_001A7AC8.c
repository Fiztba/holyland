#include "common.h"
#include "kf_effect.h"

int func_001A1E28(int, int, int, int, u16, int);
void func_001A7A48(EffectActor *);

void func_001A7AC8(EffectActor *actor) {
    ArmedMotion *motion;

    motion = (ArmedMotion *)&actor->motion;
    func_001A1E28(actor->def->end_effect, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
    func_001A7A48(actor);
    motion->unk22 = 1;
}
