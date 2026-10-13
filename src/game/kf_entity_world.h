#ifndef KF_ENTITY_WORLD_H
#define KF_ENTITY_WORLD_H

#include "kf_entity.h"

void KF_Entity_InitFromMapSpawns(int map);
void KF_Entity_ClearFlag10InSlot(int array_index);
void KF_Entity_ResetToSpawn(Entity *entity);
void KF_Entity_ClearMapSlot(int array_index);
void KF_Entity_SetSpawnedFlags(Entity *entity);
void KF_Entity_OffsetAllInSlot(int array_index, f32 *offset);
void KF_Entity_UpdateAll(void);
void KF_Entity_DrawAll(int list);
Entity *KF_Entity_GetBySpawnId(int entity_id);
Entity *KF_Entity_GetBySpawnIdForMap(int map_index, int entity_id);

#endif
