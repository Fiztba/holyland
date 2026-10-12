#include "common.h"
#include "kf_entity.h"

s32 func_00195BD8(Entity *, void *, s32, f32);

/* Runs func_00195BD8 for one part of an attack record, unless the animation frames [frame_from, frame_to]
   lie outside the frames the part is active on. */
s32 func_00195B68(Entity *entity, EntityParamsUnk240 *unk240, s32 part, s32 frame_from, s32 frame_to, f32 angle) {
    if (unk240->unk10[part].unk24 == 0 ||
        (frame_from < unk240->unk10[part].unk24 && frame_to < unk240->unk10[part].unk24) ||
        (unk240->unk10[part].unk26 < frame_from && unk240->unk10[part].unk26 < frame_to)) {
        return 0;
    }
    return func_00195BD8(entity, unk240, part, angle);
}
