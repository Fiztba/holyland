/* Item use effect (kinds 0x46–0x52): models, motion and textures shown when those items are used. */
#ifndef KF_ITEM_EFFECT_H
#define KF_ITEM_EFFECT_H

#include "common.h"

void KF_ItemEffect_Load(void);
int KF_ItemEffect_Reset(void);
void KF_ItemEffect_Start(int kind, int marked);
void KF_ItemEffect_Update(void);
void KF_ItemEffect_Draw(void);

#endif
