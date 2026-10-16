/* VU0 macro-mode helpers that keep their matrices in VU0 registers between calls (vf1..vf4 a transform to the
   screen, vf5..vf7 and vf8..vf11 for lighting, vf12..vf14 for texture coordinates, vf21.w a scale), and a GIF DMA
   send. Nothing in the game calls any of them. */
#include "common.h"
#include "math/vu0.h"
#include <eeregs.h>

/* Transforms the point at src by the matrix in vf1..vf4, divides by w and stores it at dest as 12.4 fixed-point
   integers. */
static void Vu0_TransformToScreen(void *src, void *dest) {
    Vu_Lqc2(16, src);
    Vu_MulA(VU_XYZW, 1, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 2, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 3, VF(16)[2]);
    Vu_Madd(VU_XYZW, 17, 4, VF(16)[3]);
    Vu_Div(VF(0)[3], VF(17)[3]);
    Vu_MulBc(VU_XYZW, 26, 17, ee_vu0.q);
    Vu_Ftoi(VU_XYZW, 17, 26, 4);
    Vu_Sqc2(dest, 17);
}

/* Multiplies a normal by the matrix in vf5..vf7 and clamps each component of the result at zero. */
static void Vu0_TransformNormalClamped(void *normal, void *result) {
    Vu_Lqc2(16, normal);
    Vu_MulA(VU_XYZW, 5, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 6, VF(16)[1]);
    Vu_Madd(VU_XYZW, 17, 7, VF(16)[2]);
    Vu_MaxBc(VU_XYZW, 17, 17, VF(0)[0]);
    Vu_Sqc2(result, 17);
}

/* Applies the colour matrix in vf8..vf11 to the light factors (its fourth row added whole), multiplies by the
   colour and stores the result as integers. */
static void Vu0_ApplyLightColours(void *factors, void *colour, void *result) {
    Vu_Lqc2(16, factors);
    Vu_Lqc2(19, colour);
    Vu_MulA(VU_XYZW, 8, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 9, VF(16)[1]);
    Vu_MaddA(VU_XYZW, 10, VF(16)[2]);
    Vu_Madd(VU_XYZW, 17, 11, VF(0)[3]);
    Vu_Mul(VU_XYZW, 17, 17, 19);
    Vu_Ftoi(VU_XYZW, 17, 17, 0);
    Vu_Sqc2(result, 17);
}

/* Stores the first vector as integers, with w taken from the second vector's w. */
static void Vu0_ColourToIntWithAlpha(void *colour, void *alpha_source, void *result) {
    Vu_Lqc2(16, colour);
    Vu_Lqc2(19, alpha_source);
    Vu_Add(VU_XYZW, 17, 0, 16);
    Vu_MulBc(VU_W, 17, 0, VF(19)[3]);
    Vu_Ftoi(VU_XYZW, 17, 17, 0);
    Vu_Sqc2(result, 17);
}

/* Converts a float vector to integers; the first argument is not used. */
static void Vu0_ColourToInt(void *unused, void *source, void *result) {
    Vu_Lqc2(16, source);
    Vu_Ftoi(VU_XYZW, 17, 16, 0);
    Vu_Sqc2(result, 17);
}

/* Loads a 4x4 matrix into vf8..vf11. */
static void Vu0_LoadVf8ToVf11(void *matrix) {
    Vu_Lqc2(8, (u8 *)matrix + 0x0);
    Vu_Lqc2(9, (u8 *)matrix + 0x10);
    Vu_Lqc2(10, (u8 *)matrix + 0x20);
    Vu_Lqc2(11, (u8 *)matrix + 0x30);
}

/* Loads the first three rows of a matrix into vf5..vf7. */
static void Vu0_LoadVf5ToVf7(void *matrix) {
    Vu_Lqc2(5, (u8 *)matrix + 0x0);
    Vu_Lqc2(6, (u8 *)matrix + 0x10);
    Vu_Lqc2(7, (u8 *)matrix + 0x20);
}

/* Loads a 4x4 matrix into vf1..vf4. */
static void Vu0_LoadVf1ToVf4(void *matrix) {
    Vu_Lqc2(1, (u8 *)matrix + 0x0);
    Vu_Lqc2(2, (u8 *)matrix + 0x10);
    Vu_Lqc2(3, (u8 *)matrix + 0x20);
    Vu_Lqc2(4, (u8 *)matrix + 0x30);
}

/* The same as Vu0_LoadVf5ToVf7. */
static void func_00207690(void *matrix) {
    Vu_Lqc2(5, (u8 *)matrix + 0x0);
    Vu_Lqc2(6, (u8 *)matrix + 0x10);
    Vu_Lqc2(7, (u8 *)matrix + 0x20);
}

/* The same as Vu0_LoadVf8ToVf11. */
static void func_002076A0(void *matrix) {
    Vu_Lqc2(8, (u8 *)matrix + 0x0);
    Vu_Lqc2(9, (u8 *)matrix + 0x10);
    Vu_Lqc2(10, (u8 *)matrix + 0x20);
    Vu_Lqc2(11, (u8 *)matrix + 0x30);
}

/* Loads a 4x4 matrix into vf12..vf15. */
static void Vu0_LoadVf12ToVf15(void *matrix) {
    Vu_Lqc2(12, (u8 *)matrix + 0x0);
    Vu_Lqc2(13, (u8 *)matrix + 0x10);
    Vu_Lqc2(14, (u8 *)matrix + 0x20);
    Vu_Lqc2(15, (u8 *)matrix + 0x30);
}

/* The same as Vu0_LoadVf1ToVf4. */
static void func_002076D0(void *matrix) {
    Vu_Lqc2(1, (u8 *)matrix + 0x0);
    Vu_Lqc2(2, (u8 *)matrix + 0x10);
    Vu_Lqc2(3, (u8 *)matrix + 0x20);
    Vu_Lqc2(4, (u8 *)matrix + 0x30);
}

/* Sets vf21 to (0, 0, 0, 1/2), the scale the texture-coordinate helpers use (vf20 is left as (1, 1, 1, 2)). */
static void Vu0_SetHalfScale(void) {
    Vu_AddBc(VU_XYZW, 20, 0, VF(0)[3]);
    Vu_Div(VF(0)[3], VF(20)[3]);
    Vu_Mul(VU_XYZW, 21, 0, 0);
    Vu_MulBc(VU_W, 21, 0, ee_vu0.q);
}

/* Vu0_TexCoordFromNormal for the vector already in vf16. */
static void Vu0_TexCoordFromLoaded(void *result) {
    Vu_MulA(VU_XYZW, 12, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 13, VF(16)[1]);
    Vu_Madd(VU_XYZW, 17, 14, VF(16)[2]);
    Vu_AddBc(VU_X | VU_Y, 17, 17, VF(0)[3]);
    Vu_MulBc(VU_X | VU_Y, 17, 17, VF(21)[3]);
    Vu_AddBc(VU_Z, 17, 0, VF(0)[3]);
    Vu_Sqc2(result, 17);
}

/* Transforms the vector at src by vf12..vf14 and maps x and y from -1..1 to 0..1 (times vf21.w); z becomes 1. */
static void Vu0_TexCoordFromNormal(void *dest, void *src) {
    Vu_Lqc2(16, src);
    Vu_MulA(VU_XYZW, 12, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 13, VF(16)[1]);
    Vu_Madd(VU_XYZW, 17, 14, VF(16)[2]);
    Vu_AddBc(VU_X | VU_Y, 17, 17, VF(0)[3]);
    Vu_MulBc(VU_X | VU_Y, 17, 17, VF(21)[3]);
    Vu_AddBc(VU_Z, 17, 0, VF(0)[3]);
    Vu_Sqc2(dest, 17);
}

/* Vu0_TexCoordFromNormal with the arguments the other way round (src, dest). */
static void func_00207748(void *src, void *dest) {
    Vu_Lqc2(16, src);
    Vu_MulA(VU_XYZW, 12, VF(16)[0]);
    Vu_MaddA(VU_XYZW, 13, VF(16)[1]);
    Vu_Madd(VU_XYZW, 17, 14, VF(16)[2]);
    Vu_AddBc(VU_X | VU_Y, 17, 17, VF(0)[3]);
    Vu_MulBc(VU_X | VU_Y, 17, 17, VF(21)[3]);
    Vu_AddBc(VU_Z, 17, 0, VF(0)[3]);
    Vu_Sqc2(dest, 17);
}

/* Waits until DMA channel 2 and the GIF are idle, then sends qword_count quadwords from the scratchpad
   address src_addr to the GIF. */
static void Gif_SendFromScratchpad(s32 src_addr, s32 qword_count) {
    while ((*D2_CHCR & 0x100) || (*GIF_STAT & 0xC00)) {
    }
    *D2_QWC = qword_count;
    *D2_MADR = (src_addr & 0x7FFFFFFF) | 0x80000000;
    *D2_CHCR = 0x105;
}
