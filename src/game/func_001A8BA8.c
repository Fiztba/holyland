#include "kf_effect.h"
#include "kf_entity.h"

extern char D_00413F80[];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
int func_001AFD08(void *, void *, void *);
void KF_Vector_Subtract(void *, void *, void *);
double sqrt(double);

/* Starts an effect aimed at a target model: its range is the distance still to cover. */
void func_001A8BA8(EffectActor *actor, int frame) {
    ReseedMotion *motion;
    Vector4 diff;
    double remaining;
    float range;

    motion = (ReseedMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
    actor->unk6C = func_001AFD08(actor, D_00413F80, D_00413F80 + 0x10);
    motion->unk14 = 0;
    if (actor->unk6C != 0) {
        KF_Vector_Subtract(&diff, ((Entity *)actor->unk6C)->model->position, &actor->model->position);
        remaining = (float)actor->def->interval - sqrt(diff.x * diff.x + diff.z * diff.z);
        range = remaining / (2.0f * actor->def->interval);
        motion->unk10 = range;
        motion->unk10 = range * actor->def->unkDC;
    } else {
        motion->unk10 = 0.0f;
    }
}
