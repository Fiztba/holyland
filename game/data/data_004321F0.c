/* data_004321F0.c: the game's data from the decomp's data_004321F0.data.s, defined in C for the PC (splat had it in asm) */
#include "common.h"
#include "map/map.h"

/* 0x004321F0. The decomp's labels map_slot_record_models lie inside it (map_slot_memory[0].record_models): the code that names them must use these
   fields, so they are not defined on their own */
MapSlotMemory map_slot_memory[2] __attribute__((aligned(16)));
