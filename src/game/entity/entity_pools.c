/* The entity pools, their slots and the model work area. */
#include "entity/entity.h"
#include "model/model.h"

ModelInstance entity_models[ENTITY_POOL_SLOT_COUNT] = { 0 }; /* 0x3BE530 a model per pool slot */

Entity entity_pool[2][0x80] = { 0 }; /* 0x3BEDF0 the entities of both map slots */
/* The last entity of entity_pool (0x3D6C70, the 256th), which other files name on its own as
   entity_pool_last, was a symbol set by asm at entity_pool + 0x17E80. C cannot name an array element
   as a symbol: on the PC those files compare with &entity_pool[1][0x7F] instead. */

EntityPoolSlot entity_pool_slots[ENTITY_POOL_SLOT_COUNT] = { 0 }; /* 0x3D6DF0 the pool slots */

/* PORT: holds runtime structs, may need more room on the PC (ModelMotion_LoadFile puts the model's
   ModelChannel after the saved GS words, Entity_TryActivate) */
s64 entity_work_area[0x2000] = { 0 }; /* 0x3D6E40 the work area Model_SaveGsWordsAndUvs builds a model in */
