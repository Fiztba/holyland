#include "kf_effect.h"

extern f32 D_00446100;
extern f32 D_00446104[2];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
void KF_Vector_Scale(void *, void *, float);
void func_00216C48(void *, void *, void *);

/* Starts the effect off its model's heading turned by the caller's step count, moving along it. */
void func_001ABCF8(EffectActor *actor, TurningMotion *caller) {
    ReseedMotion *motion;
    f32 offset[4];
    f32 angle;

    func_001AF928(actor, (int)caller);
    motion = (ReseedMotion *)&actor->motion;
    func_001AF640(actor, (int)caller);
    angle = actor->model->rotation.y + (actor->def->unkA6 - caller->unk25) * actor->def->unkDC;
    angle -= (int)(angle / D_00446100) * D_00446100;
    if (D_00446104[0] < angle) {
        angle -= D_00446100;
    } else if (angle < D_00446104[1]) {
        angle += D_00446100;
    }
    actor->model->rotation.y = angle;
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    KF_Vector_Scale(offset, motion, actor->def->unkA6 - caller->unk25);
    func_00216C48(&actor->model->position, &actor->model->position, offset);
    motion->unk14 = 0;
    motion->unk10 = 1.0f;
}
