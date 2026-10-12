#include "kf_effect.h"

extern u8 D_00413FBC[];
extern int D_004468E4;

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
s16 func_001B0208(float, void *, void *, void *);
int func_001A1E28(int, int, int, int, u16, int);
void func_001A2AF8(void *);

/* Per-frame update that, for map kind 0x26, steers the effect and spawns its follow-up every interval frames. */
void func_001AA470(EffectActor *actor, int frame) {
    TurningMotion *motion;
    ModelInstance *model;
    s16 angle;

    motion = (TurningMotion *)&actor->motion;
    if (D_00413FBC[0] != 0x26) {
        func_001A2AF8((void *)frame);
        return;
    }
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    actor->model->position.y -= 220.0f;
    angle = func_001B0208((float)actor->def->radius_half, actor, motion, motion->unk10);
    motion->unk24 = 0;
    motion->unk22 = angle;
    motion->unk20 = actor->def->end_effect;
    D_004468E4 += 1;
    if (D_004468E4 >= actor->def->interval) {
        model = actor->model;
        func_001A1E28(actor->def->unkF0, (int)&model->position, (int)&model->rotation, actor->effect_arg, actor->effect_param, (int)actor->owner);
        D_004468E4 = 0;
    }
}
