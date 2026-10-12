#include "kf_entity.h"

extern int D_004468CC;

/* Frees a map-object slot: clears its live bit and drops the live count. */
void func_001A2AF8(ObjectSlot *slot) {
    OBJECT_SLOT_FLAG_BITS(slot)->bit32 = 0;
    D_004468CC--;
}
