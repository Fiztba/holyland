#include "kf_effect.h"

extern EffectActor D_003F3550[];

int func_001A1E28(int, int, int, int, u16, int);

/* Marks every active effect of this actor kind, then spawns effect 0x47 at the actor. */
void func_001A88A8(EffectActor *actor) {
    EffectActor *slot;
    int i;

    if (((MarkedMotion *)&actor->motion)->unk26 != 0) {
        return;
    }
    slot = D_003F3550;
    for (i = 0; i < 0x180; i++, slot++) {
        if (slot->flags & (0x8000L << 41)) {
            if (slot->def->unk100 == actor->def->unk100) {
                ((MarkedMotion *)&slot->motion)->unk26 = 5;
            }
        }
    }
    func_001A1E28(0x47, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
}
