#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Re-seeds a ripple effect's motion and clears its spawn index. */
void func_001AE570(EffectActor *actor, int frame) {
    RippleMotion *motion;

    motion = (RippleMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    motion->spawn_index = 0;
}
