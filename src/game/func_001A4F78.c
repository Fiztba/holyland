#include "kf_effect.h"
#include "kf_entity.h"

extern char D_00413F80[];

void func_001AF928(void *, int);
void func_001AF640(void *, int);
void func_001AF538(float *, float *, float);
int func_001AFD08(void *, void *, void *);
int func_001C0E40(float, void *);
void func_00216778(void *, void *);

/* Starts an effect that follows another one: copies its motion and counts down, then settles on the ground. */
void func_001A4F78(EffectActor *actor, int frame) {
    SeekingMotion *motion;
    SeekingMotion *leader_motion;

    motion = (SeekingMotion *)&actor->motion;
    leader_motion = (SeekingMotion *)&actor->target->motion;
    func_001AF640(actor, frame);
    func_001AF928(actor, frame);
    if (actor->target->def->unkF2 != 0) {
        func_001AF538(&motion->velocity.x, &actor->model->rotation.x, actor->def->speed);
        actor->unk6C = func_001AFD08(actor, D_00413F80, D_00413F80 + 0x10);
        motion->count = actor->target->def->unkF2;
    } else {
        func_00216778(motion, leader_motion);
        actor->unk6C = actor->target->unk6C;
        motion->count = leader_motion->count - 1;
        if (motion->count == 0) {
            actor->frame = actor->def->restart_frame;
        }
    }
    actor->model->position.y -= actor->def->unkE0 * 0.5f;
    if (func_001C0E40(actor->def->unkE0, &actor->model->position) == 0) {
        actor->frame = actor->def->restart_frame;
        actor->model->position.y += actor->def->unkE0 * 0.5f;
    } else {
        actor->model->position.y = *(f32 *)0x70000004;
    }
    motion->state = 0;
    motion->radius_half = actor->def->radius_half;
}
