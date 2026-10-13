#ifndef KF_GOLD_H
#define KF_GOLD_H

#include "common.h"
#include "kf_entity.h"
#include "kf_map.h"

void KF_Gold_Init(void);
void KF_Gold_InitMap(int map);
void KF_Gold_UnlinkMap(int slot);
void KF_Gold_MoveMap(int slot, f32 *offset);
void KF_Gold_Update(void);
void KF_Gold_Draw(s32 list);
int KF_Gold_Drop(Vector4 *position, Vector4 *rotation, int map, int amount, int object_id, int on_ground,
                 int other_map);
int KF_Gold_DropFromEntity(Entity *entity);

#endif
