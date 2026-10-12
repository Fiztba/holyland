#include "kf_effect.h"

extern EffectActor D_003F3550[];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
s16 func_001B0208(float, void *, void *, void *);
int func_001A1E28(int, int, int, int, u16, int);

/* Starts a falling effect and, if no same-kind effect is already live inside its interval, spawns its follow-up. */
void func_001A9578(EffectActor *actor, int frame) {
    FallingMotion *motion;
    EffectActor *slot;
    int i;
    char scratch[0x10];

    motion = (FallingMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    motion->end_frame = func_001B0208((float)actor->def->radius_half, actor, motion, scratch);
    if (motion->end_frame != 0xFFFF) {
        motion->mode = *(int *)0x70000018;
    }
    motion->unk17[0] = 0;
    motion->spawned = 0;
    slot = D_003F3550;
    for (i = 0; i < 0x180; i++, slot++) {
        if (slot->flags & (0x8000L << 41)) {
            if (slot->def->unk100 == actor->def->unk100 && slot->frame < actor->def->interval) {
                if (((FallingMotion *)&slot->motion)->spawned != 0) {
                    return;
                }
            }
        }
    }
    func_001A1E28(actor->def->unkF0, (int)&actor->model->position, (int)&actor->model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
    motion->spawned = 1;
}
