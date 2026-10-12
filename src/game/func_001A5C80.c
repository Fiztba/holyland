#include "kf_effect.h"

void func_001AF640(void *, int);
void func_001AF928(void *, int);

/* Runs the effect's frame update for one frame, then re-seeds its motion. */
void func_001A5C80(EffectActor *actor, int frame) {
    func_001AF640(actor, frame);
    func_001AF928(actor, frame);
}
