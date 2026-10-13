#ifndef KF_MAGIC_LADDER_H
#define KF_MAGIC_LADDER_H

#include "kf_entity.h"

int KF_Player_CastMagic(int magic_id);
void KF_Player_HoldMagic(int magic_id);
void KF_Ladder_Start(ObjectReference *object, int from_far_end);
void KF_Ladder_CancelApproach(void);
int KF_Ladder_DispatchPad(u16 held, u16 pressed);
void KF_Ladder_WalkToFoot(void);
void KF_Ladder_WaitOnRung(u16 buttons);
void KF_Ladder_ClimbRungUp(u16 buttons);
void KF_Ladder_ClimbRungDown(u16 buttons);
int KF_Ladder_PlayStepSound(void);

#endif
