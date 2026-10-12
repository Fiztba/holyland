#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_0019CE30(int, void *);

/* Re-seeds an effect's motion, then places its model using the definition's radius byte. */
void func_001AEBF8(EffectActor *actor, int frame) {
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_0019CE30((u8)actor->def->radius_half, &actor->model->position);
}
