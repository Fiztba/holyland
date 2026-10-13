#ifndef KF_DEBUG_H
#define KF_DEBUG_H

#include "common.h"

void KF_Debug_EditInt(s32 *value, s32 minimum, s32 maximum, s32 step);
void KF_Debug_EditU16(u16 *value, u16 minimum, u16 maximum, u16 step);
void KF_Debug_EditU8(u8 *value, u8 minimum, u8 maximum, u8 step);
void KF_Debug_EditFloat(f32 *value, f32 minimum, f32 maximum, f32 step);

#endif
