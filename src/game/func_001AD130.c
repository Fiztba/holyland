#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);

void func_001AD130(EffectActor *actor, int frame) {
    FollowerMotion *motion;
    EffectActor *target;
    float speed;

    target = actor->target;
    motion = (FollowerMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    speed = target->motion.velocity.x;
    motion->speed = speed;
    func_001AF538(&motion->velocity.x, &actor->model->rotation.x, speed);
    motion->unk14 = 0;
    motion->unk18 = 1.0f;
    motion->unk1C = 0;
}
