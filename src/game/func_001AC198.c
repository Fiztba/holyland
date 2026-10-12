#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Runs the effect setup, then copies the target's scale_per_mille view into the actor's. */
void func_001AC198(EffectActor *actor, int frame) {
    EffectMotion *source;
    EffectMotion *dest;

    source = &actor->target->motion;
    dest = &actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    ((ScalerMotion *)dest)->scale_per_mille = ((ScalerMotion *)source)->scale_per_mille;
}
