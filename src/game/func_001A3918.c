#include "common.h"
#include "kf_entity.h"

extern ObjectSlot D_003F1750[];
extern int D_004468CC;

void func_001A2AF8(void *);

void func_001A3918(int object_id) {
    ObjectSlot *entry;
    int i;

    entry = D_003F1750;
    for (i = 0; i < 0x60 && D_004468CC > 0; i++, entry++) {
        if ((entry->flags & (0x8000L << 17)) && entry->object_id == object_id) {
            func_001A2AF8(entry);
        }
    }
}
