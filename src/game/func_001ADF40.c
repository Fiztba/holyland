#include "common.h"
#include "kf_effect.h"

void func_001AF928(void *, int);
void func_001AF640(void *, int);

/* Converts the model's heading from radians to degrees and stores it in the motion's first words. */
void func_001ADF40(EffectActor *actor, int frame) {
    HeadingWordMotion *motion;
    s32 degrees;

    motion = (HeadingWordMotion *)&actor->motion;
    func_001AF928(actor, frame);
    func_001AF640(actor, frame);
    degrees = (actor->model->rotation.x * 180.0f) / 3.1415f;
    motion->heading = degrees;
    motion->heading_word = motion->heading;
}
