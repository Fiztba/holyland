/* Basic types shared by the game sources. */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "port/host.h"

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;            /* a long on the EE, where long is 64-bit; the PC port spells it out */
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef unsigned __int128 Uint128; /* a 128-bit integer, moved with one lq/sq on the EE */

#ifndef NULL
#define NULL ((void *)0)
#endif

#define TRUE 1
#define FALSE 0

#define ABS(x) ((x) >= 0 ? (x) : -(x))

#endif
