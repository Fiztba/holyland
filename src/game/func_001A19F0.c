#include "kf_effect.h"
#include "kf_entity.h"

extern ObjectSlot D_003F1750[];
extern EffectActor D_003F3550[];
extern char D_00413F80[];
extern char D_0043EF20[];
extern char D_0043EF38[];
extern int D_004468CC;
extern int D_004468D0;

int printf(char *, ...);
void func_001A2AF8(void *);
void func_001A2A18(void *);

/* Logs and releases every live object slot whose owner is not the default one. */
void func_001A19F0(void) {
    ObjectSlot *slot = D_003F1750;
    EffectActor *wide = D_003F3550;
    int i;

    for (i = 0; i < 0x60; i++, slot++) {
        if (slot->flags & (0x8000L << 17)) {
            printf(D_0043EF20, i, slot->owner);
            if ((slot->flags & (0x8000L << 17)) != 0 && slot->owner != (Entity *)D_00413F80) {
                func_001A2AF8(slot);
                if (D_004468CC > 0) {
                    continue;
                }
                break;
            }
        }
    }
    for (i = 0; i < 0x180; i++, wide++) {
        if (wide->flags & (0x8000L << 41)) {
            printf(D_0043EF38, i, EFFECT_SOURCE(wide));
            if ((wide->flags & (0x8000L << 41)) != 0 && EFFECT_SOURCE(wide) != (Entity *)D_00413F80) {
                func_001A2A18(wide);
                if (D_004468D0 > 0) {
                    continue;
                }
                break;
            }
        }
    }
}
