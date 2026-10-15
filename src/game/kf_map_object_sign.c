#include "kf_model.h"
#include "kf_splash.h"
#include "kf_message.h"

extern u32 D_0038CC74[];
extern void *D_00420D50[];

/* Start handler of the message kind: takes the message (the definition gives the
   default) and a flag from its source. */
void func_00120D90(MapObject *object) {
    s32 *work = object->work;
    MapObjectUnk08Words *source = MAP_OBJECT_WORDS(object);

    work[1] = source->unk44;
    if (work[1] == 0) {
        work[1] = (s16)object->unk04->unk68;
    }
    object->work[0] = source->unk46;
    MAP_OBJECT_BYTES(object)->step = 1;
}

/* Update handler of the message kind at rest: used by the player, it goes to
   step 2 when its flag is set, else to step 3. */
void func_00120DC8(MapObject *object) {
    MapObject *used;

    if (((D_0038CC74[0] >> 1) & 1) && (used = D_00420D50[0]) == object) {
        if (used->work[0] != 0) {
            MAP_OBJECT_BYTES(used)->step = 2;
        } else {
            MAP_OBJECT_BYTES(used)->step = 3;
        }
    }
}

/* Update handler of the message kind, step 2: back to rest once Splash_Start accepts the message. */
void func_00120E10(MapObject *object) {
    if (Splash_Start(object->work[1])) {
        MAP_OBJECT_BYTES(object)->step = 1;
    }
}

/* Update handler of the message kind, step 3: shows the message for 45 frames. */
void func_00120E48(MapObject *object) {
    s32 *work = object->work;

    Message_ShowNotice(0, 0, 0, 0, 0x2D, work[1]);
    work[2] = 0x2D;
    MAP_OBJECT_BYTES(object)->step = 4;
}

/* Update handler of the message kind, step 4: back to rest when the 45 frames are over. */
void func_00120EA0(MapObject *object) {
    s32 *work = object->work;

    if (--work[2] <= 0) {
        MAP_OBJECT_BYTES(object)->step = 1;
    }
}

/* Start handler: step 1. */
void func_00120EC8(MapObject *object) {
    MAP_OBJECT_BYTES(object)->step = 1;
}

void func_00120ED8(void) {
}
