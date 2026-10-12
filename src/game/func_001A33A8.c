#include "kf_effect.h"
#include "kf_entity.h"

extern ObjectSlot D_003F1750[];
extern EffectActor D_003F3550[];
extern int D_004468CC;
extern int D_004468D0;

void func_001A2AF8(void *);
void func_001A2A18(void *);

/* Releases every live object slot and effect actor that belongs to the given owner. */
void func_001A33A8(Entity *owner) {
    ObjectSlot *slot = D_003F1750;
    EffectActor *effect = D_003F3550;
    int i;

    for (i = 0; i < 0x60 && D_004468CC > 0; i++, slot++) {
        if ((slot->flags & (0x8000L << 17)) && slot->object_id == 0xFFFF && slot->owner == owner) {
            func_001A2AF8(slot);
        }
    }
    for (i = 0; i < 0x180 && D_004468D0 > 0; i++, effect++) {
        if ((effect->flags & (0x8000L << 41)) && effect->effect_param == 0xFFFF && EFFECT_SOURCE(effect) == owner) {
            func_001A2A18(effect);
        }
    }
}
