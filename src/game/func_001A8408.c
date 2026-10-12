#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Re-seeds the motion from the model and definition, then clears the byte at 0x24. */
void func_001A8408(EffectActor *actor, int frame) {
    EffectMotion *motion;

    motion = &actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    ((TurningMotion *)motion)->unk24 = 0;
    actor->flags |= (s64)0x8000 << 0x2B;
}
