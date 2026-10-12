#include "common.h"
#include "kf_effect.h"
#include "kf_entity.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
int func_001C1138(float, void *);

/* Drops the model onto the entity it is tied to, and clears the motion's flag while it is in reach. */
void func_001ACD18(EffectActor *actor, int frame) {
    GroundedMotion *motion;
    Entity *target;

    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    motion = (GroundedMotion *)&actor->motion;
    motion->grounded = 0;
    target = EFFECT_SOURCE(actor);
    actor->model->position.y -= target->height;
    func_001C1138(target->height, &actor->model->position);
    if (*(int *)0x70000010 != 0) {
        actor->model->position.y = *(float *)0x70000004;
    } else {
        motion->grounded = 1;
    }
}
