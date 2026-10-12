#include "kf_effect.h"

void func_001AF640(void *, int);
void func_001AF928(void *, int);
void func_001AF538(float *, float *, float);
void func_00216778(void *, void *);
int func_001C0E40(float, void *);

/* Starts an effect that copies the motion of the effect it follows, counting down from it, and rests on the ground. */
void func_001AB188(EffectActor *actor, int frame) {
    CountedMotion *motion;
    CountedMotion *source;

    motion = (CountedMotion *)&actor->motion;
    source = (CountedMotion *)&actor->target->motion;
    func_001AF640(actor, frame);
    func_001AF928(actor, frame);
    if (actor->target->def->unkF2 != 0) {
        motion->count = actor->target->def->unkF2;
        func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    } else {
        func_00216778(motion, source);
        motion->count = source->count - 1;
        if (motion->count == 0) {
            actor->frame = actor->def->restart_frame;
        }
    }
    actor->model->position.y -= actor->def->unkE0 * 0.5f;
    if (func_001C0E40(actor->def->unkE0, &actor->model->position) == 0) {
        actor->frame = actor->def->restart_frame;
        actor->model->position.y += actor->def->unkE0 * 0.5f;
    } else {
        actor->model->position.y = *(float *)0x70000004;
    }
    motion->flag = 0;
}
