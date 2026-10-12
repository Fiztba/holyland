#include "kf_effect.h"

void func_001A3E30(EffectActor *actor, int frame);

/* Re-seeds the actor's motion, then drops its model 220 units. */
void func_001A3EA8(EffectActor *actor, int frame) {
    func_001A3E30(actor, frame);
    actor->model->position.y -= 220.0f;
}
