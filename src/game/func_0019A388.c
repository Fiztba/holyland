#include "common.h"
#include "kf_entity.h"

/* Moves *level by delta, keeping it between 0x40 and max.
   match: max is a parameter so the 0xC0 cap stays in a register (slt/movn), as in func_0019A348 */
static inline void adjust_level(u8 *level, s32 delta, s32 max) {
    s32 value = *level + delta;

    if (value < 0x40) {
        value = 0x40;
    }
    if (value > max) {
        value = max;
    }
    *level = value;
}

/* Raises the entity's unkC0 bytes 3 to 5 by 2, each kept between 0x40 and 0xC0. */
void func_0019A388(Entity *entity) {
    adjust_level(&entity->unkC0[3], 2, 0xC0);
    adjust_level(&entity->unkC0[4], 2, 0xC0);
    adjust_level(&entity->unkC0[5], 2, 0xC0);
}
