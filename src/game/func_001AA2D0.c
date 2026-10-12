#include "common.h"
#include "kf_effect.h"

void func_001AF298(void *, void *);

/* Runs func_001AF298 while the effect's frame is inside the window its motion
   block starts with: [start_frame, end_frame) as two u16s. */
void func_001AA2D0(EffectActor *actor) {
    u8 state;
    FrameWindowMotion *window;

    state = 0;
    window = (FrameWindowMotion *)&actor->motion;
    if (actor->frame >= window->start_frame && actor->frame < window->end_frame) {
        func_001AF298(actor, &state);
    }
    actor->frame += 1;
}
