#ifndef KF_PLAYER_UPDATE_H
#define KF_PLAYER_UPDATE_H

#include "common.h"
#include "model/model.h"

/* Held weapon model vtable layout for Player_UpdateWeaponAim: a view of ModelInstance, whose pointers move the
   fields on the PC, so the padding follows that struct */
struct PlayerWeaponModel {
    char pad0[offsetof(ModelInstance, update_bone_points)];
    void *(*update_aim)(void *);    /* 0x1B4 ModelInstance.update_bone_points */
    void *(*get_point)(void *, int); /* 0x1B8 ModelInstance.get_point */
};
_Static_assert(offsetof(struct PlayerWeaponModel, update_aim) == offsetof(ModelInstance, update_bone_points), "PlayerWeaponModel is not ModelInstance");
_Static_assert(offsetof(struct PlayerWeaponModel, get_point) == offsetof(ModelInstance, get_point), "PlayerWeaponModel is not ModelInstance");

void Player_Update(void);
void Player_DrawScreenTints(void);
void Player_UpdateScreenTints(void);
void Player_UpdateActions(u16 held, u16 pressed);
void Player_SwingWeapon(u16 held, u16 pressed);
void Player_InvokeTargetHandler(u16 target_id, u16 command);
void Player_FireProjectile(u16 held, u16 pressed);
void Player_DrawBow(u16 held, u16 pressed);
void Player_PlayEquipSound(void);
void Player_FinishWeaponMotion(void);
void Player_HolsterWeapon(void);
int Player_CanFireAtAim(void);
void Player_UpdateWeaponAim(void);
void Player_Move(u16 held, u16 pressed); // match: retail passes both pad words; only held is used

#endif
