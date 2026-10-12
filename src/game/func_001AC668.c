#include "kf_effect.h"

int func_001AEC48(void *);
void func_001AED58(void *);
int func_001AF0B0(void *);
void func_001AF298(void *, void *);
void sceVu0AddVector(void *, void *, void *);

/* Steps a falling effect: gravity into its velocity, the model moved by the velocity, then the frame counter. */
void func_001AC668(EffectActor *actor) {
    EffectMotion *motion;
    u8 flag = FALSE;

    motion = &actor->motion;
    func_001AEC48(actor);
    func_001AED58(actor);
    func_001AF0B0(actor);
    motion->velocity.y += actor->def->gravity;
    sceVu0AddVector(&actor->model->position, &actor->model->position, motion);
    func_001AF298(actor, &flag);
    actor->frame++;
}
