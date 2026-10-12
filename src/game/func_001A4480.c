#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);

/* Advances the actor's frame counter and wraps it at the control block's limit. */
int func_001A4480(void *owner) {
    EffectActor *actor;

    actor = owner;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    actor->frame += 1;
    if (actor->frame >= actor->def->restart_frame) {
        actor->frame = 0;
    }
}
