#include "kf_effect.h"

void func_001AF640(void *, int);
void func_001AF928(void *, int);
void func_001A2A18(void *);
int func_001C0E40(float, void *);
int rand(void);

/* Starts an effect that rests on the ground and picks a random lifetime from its definition. */
void func_001AB870(EffectActor *actor, int frame) {
    LifetimeMotion *motion;

    motion = (LifetimeMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    actor->model->position.y -= actor->def->unkDC;
    if (func_001C0E40(actor->def->unkDC * 2, &actor->model->position) == 1) {
        actor->model->position.y = *(float *)0x70000004 - actor->def->unkDC;
    } else {
        func_001A2A18(actor);
    }
    motion->lifetime = actor->def->radius_half + rand() % actor->def->interval;
}
