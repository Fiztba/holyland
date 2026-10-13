#ifndef KF_MEMORY_H
#define KF_MEMORY_H

#include "common.h"

void KF_Memory_Init(void);
void KF_Memory_Clear64(char *dest, u32 byte_count);
void KF_Memory_ClearWords(void *dest, u32 word_count);
void KF_Memory_CopyWords(s32 *dest, s32 *src, u32 word_count);
void KF_Memory_Copy128(char *src, char *dest, u32 byte_count);
void KF_Memory_PlaceAreas(void);

#endif
